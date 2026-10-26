import base64
import hashlib
import hmac
import json
from datetime import timedelta

from django.db import transaction
from django.test import TestCase, override_settings
from django.utils import timezone

from . import logic, qpay
from .models import AuthToken, Cashout, Device, Event, GameSettings, GoldTransaction, Invoice, Player, Room, Verification

UB = 15  # Улаанбаатар


@override_settings(DEBUG=True, QPAY_MODE="mock", SMS_MODE="console")
class FlowTests(TestCase):
    def setUp(self):
        qpay.MockQPay.paid.clear()
        # Тестийн бүх хүсэлт нэг IP-ээс (127.0.0.1): ижил IP-ийн хоригийг тусгай тестэд л шалгана
        settings = GameSettings.load()
        settings.block_same_ip = False
        settings.verify_bonus = 0  # (тусгай тестэд л: бусад тестийн Gold-ийн тооцоо өөрчлөгдөхгүй)
        settings.save()

    # ---- туслахууд
    def post(self, path, data=None, token=None, status=200):
        headers = {"HTTP_AUTHORIZATION": f"Bearer {token}"} if token else {}
        response = self.client.post(f"/api/{path}", data or {}, content_type="application/json", **headers)
        self.assertEqual(response.status_code, status, response.content)
        return response.json()

    def get(self, path, token, status=200):
        response = self.client.get(f"/api/{path}", HTTP_AUTHORIZATION=f"Bearer {token}")
        self.assertEqual(response.status_code, status, response.content)
        return response.json()

    def sms_sent(self, verification_id):
        """Тоглогч 144773 руу мессеж илгээсэн мэт (console горим: тестэд л)."""
        verification = Verification.objects.get(pk=verification_id)
        verification.payload = {**verification.payload, "dev_verified": True}
        verification.save(update_fields=["payload"])

    def register(self, phone="99112233", device="device-aaaa-1111", name="Бат"):
        start = self.post("register/start/", {"name": name, "phone": phone, "password": "secret1", "device_id": device,
                                              "platform": "windows"})
        self.assertEqual(start["sms"]["shortcode"], "144773")
        self.sms_sent(start["verification_id"])
        return self.post("register/confirm/", {"verification_id": start["verification_id"]})

    def pay(self, invoice_id):
        """QPay дээр төлөгдсөн мэт (mock): тоглоомын асуулгаар эрх олгогдоно."""
        qpay.MockQPay.paid.add(Invoice.objects.get(pk=invoice_id).qpay_invoice_id)
        logic.refresh_invoice(Invoice.objects.get(pk=invoice_id), "test")

    # ---- бүртгэл
    def test_register_gives_start_gold(self):
        result = self.register()
        account = result["account"]
        self.assertEqual(account["gold"], 500)
        self.assertEqual(account["upgrades"], {"damage": 0, "fire_rate": 0, "magazine": 0, "health": 0, "soldier": 0})
        self.assertTrue(account["device_ok"])
        self.assertEqual(account["phone"], "+976****2233")
        self.assertTrue(Event.objects.filter(kind="registered").exists())

    def test_one_account_per_phone_and_device(self):
        self.register()
        self.assertEqual(self.post("register/start/", {"name": "Дорж", "phone": "99112233", "password": "secret1",
                                                       "device_id": "device-bbbb-2222"}, status=409)["error"], "phone_taken")
        self.assertEqual(self.post("register/start/", {"name": "Дорж", "phone": "88112233", "password": "secret1",
                                                       "device_id": "device-aaaa-1111"}, status=409)["error"], "device_taken")

    def test_confirm_waits_for_sms(self):
        start = self.post("register/start/", {"name": "Бат", "phone": "99112233", "password": "secret1",
                                              "device_id": "device-aaaa-1111"})
        self.assertEqual(self.post("register/confirm/", {"verification_id": start["verification_id"]}, status=409)["error"],
                         "not_verified")
        self.assertFalse(Player.objects.exists())
        self.sms_sent(start["verification_id"])
        self.post("register/confirm/", {"verification_id": start["verification_id"]})
        # Нэг баталгаажуулалтаар хоёр дахь удаа данс үүсгэхгүй
        self.assertEqual(self.post("register/confirm/", {"verification_id": start["verification_id"]}, status=410)["error"],
                         "code_expired")

    # ---- аймаг: бүгд үнэгүй
    def test_all_provinces_are_free(self):
        token = self.register()["token"]
        account = self.get("account/", token)["account"]
        self.assertEqual(account["provinces"], list(range(22)))
        self.assertIsNone(self.post("provinces/claim/", {"province": UB}, token)["source"])
        self.post("provinces/start/", {"province": 0}, token)
        self.post("provinces/start/", {"province": UB}, token)
        self.assertEqual(self.post("provinces/start/", {"province": 22}, token, status=400)["error"], "bad_province")
        # Аймгийн багц, Улаанбаатарыг худалдахгүй
        self.assertEqual(self.post("invoices/", {"kind": "pack"}, token, status=410)["error"], "not_for_sale")
        self.assertEqual(self.post("invoices/", {"kind": "capital"}, token, status=410)["error"], "not_for_sale")

    # ---- Gold, дэлгүүр
    def test_signup_gold_comes_from_settings(self):
        settings = GameSettings.load()
        settings.start_gold = 700
        settings.save()
        account = self.register()["account"]
        self.assertEqual(account["gold"], 700)
        self.assertEqual(GoldTransaction.objects.get().reason, "signup")

    def test_shop_buy_charges_gold_once(self):
        token = self.register()["token"]
        account = self.post("shop/buy/", {"item": "damage", "level": 0}, token)["account"]
        self.assertEqual((account["gold"], account["upgrades"]["damage"]), (440, 1))
        # Ижил хүсэлт давтагдсан (хариу ирээгүй мэт): дахин хасахгүй
        account = self.post("shop/buy/", {"item": "damage", "level": 0}, token)["account"]
        self.assertEqual((account["gold"], account["upgrades"]["damage"]), (440, 1))
        # Дараагийн түвшин 2 дахин үнэтэй
        account = self.post("shop/buy/", {"item": "damage", "level": 1}, token)["account"]
        self.assertEqual((account["gold"], account["upgrades"]["damage"]), (320, 2))
        # Хуучин түвшинтэй хүсэлт (өөр төхөөрөмж дээр авсан г.м.) татгалзана
        self.assertEqual(self.post("shop/buy/", {"item": "damage", "level": 0}, token, status=409)["error"], "shop_changed")
        player = Player.objects.get()
        self.assertEqual(player.gold, 320)
        self.assertEqual(sum(t.amount for t in player.gold_transactions.all()), player.gold)

    def test_shop_limits(self):
        token = self.register()["token"]
        player = Player.objects.get()
        player.soldiers = 10
        player.gold = 30
        player.save()
        self.assertEqual(self.post("shop/buy/", {"item": "soldier", "level": 10}, token, status=409)["error"], "max_level")
        # Амь 50 x 1 = 50 Gold > 30
        self.assertEqual(self.post("shop/buy/", {"item": "health", "level": 0}, token, status=402)["error"], "not_enough_gold")
        self.assertEqual(self.post("shop/buy/", {"item": "armor", "level": 0}, token, status=400)["error"], "bad_item")
        self.assertEqual(self.post("shop/buy/", {"item": "damage", "level": "0"}, token, status=400)["error"], "bad_item")
        player.refresh_from_db()
        self.assertEqual((player.gold, player.health_level), (30, 0))

    def test_soldiers_flat_price_and_lost_in_match(self):
        token = self.register()["token"]
        # Цэрэг бүр ижил үнэтэй (120 Gold)
        account = self.post("shop/buy/", {"item": "soldier", "level": 0}, token)["account"]
        account = self.post("shop/buy/", {"item": "soldier", "level": 1}, token)["account"]
        self.assertEqual((account["gold"], account["upgrades"]["soldier"]), (260, 2))
        # Тоглолтод нэг нь үхэв: шууд хасагдана; давтан илгээсэн хүсэлт дахин хасахгүй, нэмэгдүүлэхгүй
        account = self.post("soldiers/lost/", {"soldiers": 1}, token)["account"]
        self.assertEqual(account["upgrades"]["soldier"], 1)
        self.assertEqual(self.post("soldiers/lost/", {"soldiers": 1}, token)["account"]["upgrades"]["soldier"], 1)
        self.assertEqual(self.post("soldiers/lost/", {"soldiers": 5}, token)["account"]["upgrades"]["soldier"], 1)
        # Дахин авахад (ижил түвшин 2) Gold хасагдана (өмнөх худалдаатай давхцахгүй)
        account = self.post("shop/buy/", {"item": "soldier", "level": 1}, token)["account"]
        self.assertEqual((account["gold"], account["upgrades"]["soldier"]), (140, 2))
        self.assertEqual(self.post("soldiers/lost/", {"soldiers": -1}, token, status=400)["error"], "bad_request")
        player = Player.objects.get()
        self.assertEqual((player.soldiers, player.soldiers_lost), (2, 1))

    # ---- Gold татах
    def give(self, amount, reason, ref):
        with transaction.atomic():
            player = Player.objects.select_for_update().get()
            logic.change_gold(player, amount, reason, ref)

    def cashout(self, token, gold, status=200, bank="Хаан банк", account="5012 345678", holder="Бат Болд"):
        return self.post("cashouts/", {"gold": gold, "bank": bank, "account": account, "holder": holder}, token, status=status)

    def test_cashout_only_won_gold(self):
        token = self.register()["token"]
        # Цэнэглэсэн Gold татагдахгүй
        self.give(30000, GoldTransaction.PURCHASE, "test:buy")
        self.assertEqual(self.get("account/", token)["account"]["cashout"]["withdrawable"], 0)
        self.assertEqual(self.cashout(token, 20000, status=402)["error"], "cashout_too_much")
        # Тулаанд хожсон Gold татагдана; доод хэмжээ 20000
        self.give(25000, GoldTransaction.BATTLE_WIN, "test:win")
        info = self.get("account/", token)["account"]["cashout"]
        self.assertEqual((info["withdrawable"], info["min"], info["rate"]), (25000, 20000, 1.0))
        self.assertEqual(self.cashout(token, 19999, status=400)["error"], "cashout_min")
        self.assertEqual(self.cashout(token, 20000, bank="Банк", status=400)["error"], "bad_bank")
        self.assertEqual(self.cashout(token, 20000, account="12ab", status=400)["error"], "bad_account")
        self.assertEqual(self.cashout(token, 20000, holder="", status=400)["error"], "bad_holder")
        # Хүсэлт: Gold шууд хасагдана, утсаа баталгаажуулна
        start = self.cashout(token, 20000)
        self.assertEqual((start["cashout"]["status"], start["cashout"]["amount"], start["cashout"]["account"]),
                         ("verifying", 20000, "5012345678"))
        self.assertEqual(start["sms"]["shortcode"], "144773")
        self.assertEqual((start["account"]["gold"], start["account"]["cashout"]["withdrawable"]), (35500, 5000))
        self.assertEqual(self.cashout(token, 20000, status=409)["error"], "cashout_active")
        cashout_id = start["cashout"]["id"]
        self.assertEqual(self.get(f"cashouts/{cashout_id}/", token)["cashout"]["status"], "verifying")
        self.sms_sent(start["verification_id"])
        reply = self.get(f"cashouts/{cashout_id}/", token)
        self.assertEqual(reply["cashout"]["status"], "pending")
        self.assertEqual(reply["account"]["cashout"]["active"]["id"], cashout_id)
        # Баталгаажсаныг цуцлахгүй
        self.assertEqual(self.post(f"cashouts/{cashout_id}/cancel/", {}, token, status=409)["error"], "cashout_locked")
        player = Player.objects.get()
        self.assertEqual(sum(t.amount for t in player.gold_transactions.all()), player.gold)

    def test_cashout_expired_or_cancelled_refunds(self):
        token = self.register()["token"]
        self.give(45000, GoldTransaction.BATTLE_WIN, "test:win")
        first = self.cashout(token, 20000)
        Verification.objects.filter(pk=first["verification_id"]).update(expires=timezone.now() - timedelta(seconds=1))
        account = self.get("account/", token)["account"]
        self.assertEqual((account["gold"], account["cashout"]["withdrawable"], account["cashout"]["active"]), (45500, 45000, None))
        self.assertEqual(Cashout.objects.get(pk=first["cashout"]["id"]).status, "expired")
        second = self.cashout(token, 25000)
        reply = self.post(f"cashouts/{second['cashout']['id']}/cancel/", {}, token)
        self.assertEqual((reply["cashout"]["status"], reply["account"]["gold"]), ("cancelled", 45500))
        self.assertEqual(len(self.get("cashouts/", token)["cashouts"]), 2)

    def test_won_gold_never_exceeds_gold(self):
        self.register()
        self.give(1000, GoldTransaction.BATTLE_WIN, "test:win")
        self.give(-1400, GoldTransaction.SHOP, "test:shop")  # 1500 -> 100: эхлээд цэнэглэсэн (эхлэлийн) Gold зарцуулагдсан
        self.assertEqual(Player.objects.get().won_gold, 100)
        self.give(5000, GoldTransaction.PURCHASE, "test:buy")  # цэнэглэсэн нь хожсон болохгүй
        self.give(-60, GoldTransaction.BATTLE_LOSS, "test:loss")  # цэнэглэснээс: хожсон хамгаалагдана
        self.assertEqual(Player.objects.get().won_gold, 100)
        self.give(-5000, GoldTransaction.SHOP, "test:shop2")  # цэнэглэсэн дуусаад хожсоноос
        self.assertEqual(Player.objects.get().won_gold, 40)

    def test_shop_prices_come_from_database(self):
        settings = GameSettings.load()
        settings.price_magazine = 25
        settings.save()
        token = self.register()["token"]
        self.assertEqual(self.get("account/", token)["account"]["shop"]["magazine"], {"price": 25, "max": 5})
        self.assertEqual(self.post("shop/buy/", {"item": "magazine", "level": 0}, token)["account"]["gold"], 475)

    def test_callback_with_wrong_key_is_ignored(self):
        self.register()
        token = self.post("login/", {"phone": "99112233", "password": "secret1", "device_id": "device-two-2222"})["token"]
        token = self.post("login/", {"phone": "99112233", "password": "secret1", "device_id": "device-new-3333"})["token"]
        invoice = self.post("invoices/", {"kind": "device"}, token)["invoice"]
        qpay.MockQPay.paid.add(f"mock-{invoice['id']}")
        self.client.get(f"/api/qpay/callback/{invoice['id']}/wrong-key/")
        self.assertFalse(Invoice.objects.get(pk=invoice["id"]).applied)

    # ---- нээлттэй өрөө
    def played(self, room_id, token, updates=3):
        """Хост даалгавар хийж байгаа мэт: ахиц өөрчлөгдөж серверт очно (үе шатны шагналд шаардлагатай)."""
        for k in range(updates):
            self.post(f"rooms/{room_id}/stage/", {"stage": f"{k + 1} 0 0 0 0", "recruits": 0}, token)

    def accept(self, room_id, host):
        """Хост: зочин тоглоомоор холбогдлоо."""
        return self.post(f"rooms/{room_id}/accept/", {}, host)

    def two_players(self):
        host = self.register()["token"]
        guest = self.register(phone="88112233", device="device-bbbb-2222", name="Дорж")["token"]
        return host, guest

    def test_enough_gold_opens_a_room_otherwise_solo(self):
        host = self.register()["token"]
        result = self.post("provinces/start/", {"province": 3, "protocol": 13}, host)
        room = result["room"]
        self.assertEqual((room["province"], room["role"]), (3, "host"))
        self.assertEqual((result["account"]["gold"], result["account"]["gold_locked"]), (480, 50))
        # Тасалбар: relay ижил нууц үгээр шалгана
        body, signature = room["ticket"].split(".")
        self.assertEqual(signature, hmac.new(b"dev-only-relay-secret", body.encode(), hashlib.sha256).hexdigest())
        payload = json.loads(base64.urlsafe_b64decode(body + "=" * (-len(body) % 4)))
        self.assertEqual((payload["r"], payload["role"]), (room["id"], "host"))
        # Дахин эхэлбэл өмнөх өрөө (апп унасан г.м.) шийтгэлгүй хаагдаж, шинэ өрөө нээгдэнэ
        result = self.post("provinces/start/", {"province": 4, "protocol": 13}, host)
        self.assertEqual((result["account"]["gold"], result["account"]["gold_locked"]), (460, 50))
        self.assertEqual(Room.objects.get(pk=room["id"]).closed_reason, "replaced")
        # 70-аас бага Gold: хаалттай өрөө (зөвхөн найз кодоор, тулаангүй), Gold өөрчлөгдөхгүй
        poor = self.register(phone="88112233", device="device-bbbb-2222", name="Дорж")["token"]
        Player.objects.filter(phone="+97688112233").update(gold=69)
        result = self.post("provinces/start/", {"province": 4, "protocol": 13}, poor)
        self.assertEqual((result["room"]["pvp"], len(result["room"]["code"])), (False, 6))
        self.assertEqual((result["account"]["gold"], result["account"]["gold_locked"]), (69, 0))
        # Хаалттай өрөөнд тулааны хайлтаар орохгүй
        third = self.register(phone="95112233", device="device-cccc-3333", name="Сүх")["token"]
        self.post("provinces/start/", {"province": 1, "protocol": 13}, host)  # (хостын өрөө: зочинтой биш, зөвхөн өөр)
        Room.objects.filter(host__phone="+97699112233").update(status=Room.CLOSED)
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, third, status=404)["error"], "no_rooms")

    def test_friends_play_together_by_code(self):
        host, guest = self.two_players()
        Player.objects.filter(phone="+97699112233").update(gold=60, bonus_gold=60)  # тулаанд Gold хүрэлцэхгүй
        room = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]
        self.assertEqual(self.post("rooms/coop/", {"code": "12a4"}, guest, status=400)["error"], "bad_code")
        self.assertEqual(self.post("rooms/coop/", {"code": room["code"], "protocol": 13}, host, status=409)["error"], "own_room")
        joined = self.post("rooms/coop/", {"code": room["code"], "protocol": 13}, guest)
        self.assertEqual((joined["room"]["role"], joined["room"]["coop"]), ("guest", True))
        self.accept(room["id"], host)
        self.played(room["id"], host)
        self.assertEqual((joined["account"]["gold"], joined["account"]["gold_locked"]), (500, 0))  # төлбөр, барьцаагүй
        # Тулаан биш
        self.assertEqual(self.post(f"rooms/{room['id']}/result/", {"battle": 0, "winner": "guest"}, guest, status=409)["error"],
                         "not_battle")
        # Үе давлаа: хоёулаа +30 (урамшуулал)
        Room.objects.filter(pk=room["id"]).update(created=timezone.now() - timedelta(minutes=30))
        self.post(f"rooms/{room['id']}/complete/", {}, host)
        h, g = Player.objects.get(phone="+97699112233"), Player.objects.get(phone="+97688112233")
        self.assertEqual((h.gold, g.gold, h.won_gold, g.won_gold), (90, 530, 0, 0))

    def test_coop_guest_leaving_is_no_penalty(self):
        host, guest = self.two_players()
        room = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]  # нээлттэй (тулаант) өрөө
        self.post("rooms/coop/", {"code": room["code"], "protocol": 13}, guest)
        account = self.post(f"rooms/{room['id']}/leave/", {}, guest)["account"]
        self.assertEqual((account["gold"], account["gold_locked"]), (500, 0))
        self.assertIsNone(Room.objects.get(pk=room["id"]).guest)
        # Хост гарвал (зочингүй үеийн дүрмээр) барьцаа шатна
        self.post(f"rooms/{room['id']}/leave/", {}, host)
        self.assertEqual(Player.objects.get(phone="+97699112233").gold, 430)

    def test_premium_skin_costs_gold_each_start(self):
        token = self.register()["token"]
        Player.objects.filter(phone="+97699112233").update(gold=60)
        # Энгийн дүр: үнэгүй (60 < 70 тул ганцаарчилсан)
        self.assertEqual(self.post("provinces/start/", {"province": 0, "skin": 1}, token)["account"]["gold"], 60)
        # Онцгой дүр (DEMON = 3): эхлүүлэх бүрд 50
        result = self.post("provinces/start/", {"province": 0, "skin": 3}, token)
        self.assertEqual((result["account"]["gold"], result["account"]["skin_price"]), (10, 50))
        self.assertEqual(GoldTransaction.objects.filter(reason=GoldTransaction.SKIN).count(), 1)
        # Gold хүрэлцэхгүй: эхлэхгүй, Gold өөрчлөгдөхгүй
        self.assertEqual(self.post("provinces/start/", {"province": 0, "skin": 4}, token, status=402)["error"],
                         "not_enough_gold")
        self.assertEqual(Player.objects.get().gold, 10)

    def test_skin_prices_and_coop_reward_follow_settings(self):
        GameSettings.objects.filter(pk=GameSettings.load().pk).update(skin_price=20, skin_price_iron=35, coop_reward=12,
                                                                       coop_daily_limit=1)
        token = self.register()["token"]
        Player.objects.filter(phone="+97699112233").update(gold=60)
        account = self.post("provinces/start/", {"province": 0, "skin": 4}, token)["account"]  # IRON
        self.assertEqual((account["gold"], account["skin_prices"], account["coop_reward"]), (25, {"3": 20, "4": 35}, 12))
        self.assertEqual(self.post("provinces/start/", {"province": 0, "skin": 3}, token)["account"]["gold"], 5)  # DEMON

    def test_coop_reward_daily_limit_from_settings(self):
        GameSettings.objects.filter(pk=GameSettings.load().pk).update(coop_reward=12, coop_daily_limit=1)
        host, guest = self.two_players()
        for expected in (12, 12):  # өдөрт 1 удаа: хоёр дахь удаа шагналгүй
            room = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]
            self.post("rooms/coop/", {"code": room["code"], "protocol": 13}, guest)
            self.accept(room["id"], host)
            self.played(room["id"], host)
            Room.objects.filter(pk=room["id"]).update(created=timezone.now() - timedelta(minutes=30))
            self.post(f"rooms/{room['id']}/complete/", {}, host)
            self.assertEqual(Player.objects.get(phone="+97688112233").gold, 500 + expected)

    def test_locked_gold_cannot_be_spent(self):
        host = self.register()["token"]
        player = Player.objects.get()
        player.gold = 120
        player.save()
        self.post("provinces/start/", {"province": 0, "protocol": 13}, host)  # 100 үлдэж, 50 нь барьцаанд
        # Цэрэг 120 > 50 (боломжтой)
        self.assertEqual(self.post("shop/buy/", {"item": "soldier", "level": 0}, host, status=402)["error"], "not_enough_gold")
        self.post("shop/buy/", {"item": "magazine", "level": 0}, host)  # 40 <= 50
        player.refresh_from_db()
        self.assertEqual((player.gold, player.locked_gold), (60, 50))

    def test_guest_joins_without_invitation(self):
        host, guest = self.two_players()
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, guest, status=404)["error"], "no_rooms")
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, host, status=404)["error"], "no_rooms")  # өөрийн өрөө биш
        joined = self.post("rooms/join/", {"protocol": 13}, guest)
        self.assertEqual((joined["room"]["id"], joined["room"]["role"], joined["room"]["host"]), (room_id, "guest", "Бат"))
        self.assertEqual((joined["account"]["gold"], joined["account"]["gold_locked"]), (480, 50))
        # Өрөө дүүрсэн
        third = self.register(phone="95112233", device="device-cccc-3333", name="Сүх")["token"]
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, third, status=404)["error"], "no_rooms")
        # Gold хүрэлцэхгүй
        player = Player.objects.get(phone="+97695112233")
        player.gold = 60
        player.save()
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, third, status=402)["error"], "not_enough_gold")

    def test_host_leaving_burns_the_stake(self):
        host = self.register()["token"]
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        account = self.post(f"rooms/{room_id}/leave/", {}, host)["account"]
        self.assertEqual((account["gold"], account["gold_locked"]), (430, 0))  # 500 - 20 - 50, хэнд ч очоогүй
        self.assertEqual(self.post(f"rooms/{room_id}/leave/", {}, host, status=410)["error"], "room_closed")
        self.assertEqual(Room.objects.get().closed_reason, "host_left")

    def test_guest_leaving_keeps_the_battle(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)
        self.accept(room_id, host)
        # Тулааны үеэр гарсан зочны багийг AI тулалдуулна: барьцаа үр дүн гартал түгжээтэй, байр сул биш
        self.assertEqual(self.post(f"rooms/{room_id}/leave/", {}, guest)["account"]["gold_locked"], 50)
        third = self.register(phone="95112233", device="device-cccc-3333", name="Сүх")["token"]
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, third, status=404)["error"], "no_rooms")
        self.assertTrue(Room.objects.get().guest_left)

    def test_completing_the_stage_pays_once(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.assertEqual(self.post(f"rooms/{room_id}/complete/", {}, host, status=409)["error"], "too_fast")
        Room.objects.filter(pk=room_id).update(created=timezone.now() - timedelta(minutes=30))
        self.played(room_id, host)
        self.post("rooms/join/", {"protocol": 13}, guest)
        self.assertEqual(self.post(f"rooms/{room_id}/complete/", {}, guest, status=403)["error"], "not_host")
        account = self.post(f"rooms/{room_id}/complete/", {}, host)["account"]
        self.assertEqual((account["gold"], account["gold_locked"]), (520, 0))  # 500 - 20 + 40
        self.assertEqual(self.post(f"rooms/{room_id}/complete/", {}, host, status=410)["error"], "room_closed")
        self.assertEqual(Player.objects.get(phone="+97688112233").locked_gold, 0)

    def test_lost_connection_is_not_a_penalty(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post(f"rooms/{room_id}/beat/", {}, host)
        Room.objects.filter(pk=room_id).update(host_seen=timezone.now() - timedelta(minutes=11))
        # Хостын дохио тасарсан: зочин тэр өрөөнд орохгүй, хостын барьцаа шийтгэлгүй буцна
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, guest, status=404)["error"], "no_rooms")
        host_player = Player.objects.get(phone="+97699112233")
        self.assertEqual((host_player.gold, host_player.locked_gold), (480, 0))
        self.assertEqual(Room.objects.get().closed_reason, "timeout")
        self.assertEqual(sum(t.amount for t in host_player.gold_transactions.all()), host_player.gold)

    # ---- тулаан
    def battle(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        joined = self.post("rooms/join/", {"protocol": 13}, guest)["room"]
        self.assertEqual(joined["battle"], 1)
        self.accept(room_id, host)
        self.assertEqual(self.post(f"rooms/{room_id}/beat/", {}, host)["guest"], "Дорж")
        return host, guest, room_id

    def gold_of(self, phone):
        player = Player.objects.get(phone=phone)
        return player.gold, player.locked_gold

    def test_host_wins_when_both_agree(self):
        host, guest, room_id = self.battle()
        first = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)
        self.assertFalse(first["settled"])
        second = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, guest)
        self.assertEqual((second["settled"], second["winner"]), (True, "host"))
        self.assertEqual(self.gold_of("+97699112233"), (530, 50))  # 500 - 20 + 50, барьцаа хэвээр (өрөөнд)
        self.assertEqual(self.gold_of("+97688112233"), (430, 0))   # 500 - 20 - 50, өрөөнөөс гарсан
        # Хожуу мэдэгдэл давхар тооцогдохгүй
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, guest, status=410)
        self.assertTrue(self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)["settled"])
        self.assertEqual(self.gold_of("+97699112233"), (530, 50))
        # Сул байранд дараагийн зочин орно, ахиц хэвээр
        self.post(f"rooms/{room_id}/stage/", {"stage": "1 3 0 0 0", "recruits": 2}, host)
        third = self.register(phone="95112233", device="device-cccc-3333", name="Сүх")["token"]
        room = self.post("rooms/join/", {"protocol": 13}, third)["room"]
        self.assertEqual((room["id"], room["battle"], room["stage"]), (room_id, 2, "1 3 0 0 0"))

    def test_free_gold_stays_free_in_battles(self):
        # Эхлэлийн Gold-оо алдсан зочноос хожсон нь урамшуулал: татагдахгүй (олон дансаар үнэгүй Gold-ийг мөнгө болгохгүй)
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, guest)
        winner = Player.objects.get(phone="+97699112233")
        self.assertEqual((winner.gold, winner.won_gold, winner.bonus_gold), (530, 0, 530))
        self.assertEqual(GoldTransaction.objects.get(reason=GoldTransaction.BATTLE_WIN).data["bonus"], 50)

    def test_purchased_gold_lost_in_battle_is_withdrawable(self):
        host, guest = self.two_players()
        Player.objects.filter(phone="+97688112233").update(bonus_gold=0)  # зочны Gold бүгд цэнэглэсэн
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)
        self.accept(room_id, host)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, guest)
        winner = Player.objects.get(phone="+97699112233")
        self.assertEqual((winner.won_gold, winner.bonus_gold), (50, 480))
        self.assertEqual(self.get("account/", host)["account"]["cashout"]["won"], 50)

    def test_stage_bonus_is_not_withdrawable(self):
        host = self.register()["token"]
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        Room.objects.filter(pk=room_id).update(created=timezone.now() - timedelta(minutes=30))
        self.played(room_id, host)
        account = self.post(f"rooms/{room_id}/complete/", {}, host)["account"]
        self.assertEqual((account["gold"], account["cashout"]["withdrawable"]), (520, 0))
        self.assertEqual(Player.objects.get().bonus_gold, 520)

    def test_guest_wins_and_becomes_host(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/stage/", {"stage": "4 15 0 0 0", "recruits": 3}, host)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest)
        result = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, host)
        self.assertEqual((result["settled"], result["winner"]), (True, "guest"))
        # Түрүүлж мэдэгдсэн зочин дахин асуухад шинэ хостын тасалбар, ахиц, ялагдагчийн цэргүүд
        polled = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest)
        self.assertEqual((polled["settled"], polled["result"]), (True, "won"))
        self.assertEqual(result["result"], "lost")
        self.assertEqual((polled["room"]["role"], polled["room"]["stage"], polled["room"]["recruits"]), ("host", "4 15 0 0 0", 3))
        self.assertEqual(self.gold_of("+97688112233"), (530, 50))  # шинэ хост, барьцаа хэвээр
        self.assertEqual(self.gold_of("+97699112233"), (430, 0))
        self.assertEqual(Room.objects.get().host.name, "Дорж")
        self.post(f"rooms/{room_id}/leave/", {}, host, status=410)  # хуучин хост өрөөнд байхгүй

    def test_disputed_result_pays_nobody(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)
        disputed = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest)
        self.assertEqual((disputed["settled"], disputed["result"]), (True, "void"))
        self.assertEqual(self.gold_of("+97699112233"), (480, 50))
        self.assertEqual(self.gold_of("+97688112233"), (480, 0))
        self.assertTrue(Event.objects.filter(kind="battle_disputed").exists())

    def test_single_report_settles_after_waiting(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/leave/", {}, guest)  # зочин гарсан: AI тулалдана
        self.assertFalse(self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)["settled"])
        Room.objects.filter(pk=room_id).update(report_at=timezone.now() - timedelta(seconds=61))
        self.assertTrue(self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)["settled"])
        self.assertEqual(self.gold_of("+97688112233"), (430, 0))

    def test_host_leaving_during_battle_hands_the_room_over(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/leave/", {}, host)
        self.assertEqual(self.gold_of("+97699112233"), (430, 0))
        self.assertEqual(self.gold_of("+97688112233"), (530, 50))
        polled = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest)
        self.assertEqual(polled["room"]["role"], "host")

    def test_same_pair_is_paid_a_few_times_a_day(self):
        settings = GameSettings.load()
        settings.battle_pair_limit = 1
        settings.save()
        host, guest, room_id = self.battle()
        for token in (host, guest):
            self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, token)
        self.post("rooms/join/", {"protocol": 13}, guest)
        self.accept(room_id, host)
        for token in (host, guest):
            self.post(f"rooms/{room_id}/result/", {"battle": 2, "winner": "host"}, token)
        # Хоёр дахь тулаанд Gold шилжээгүй (зөвхөн 20 төлбөр)
        self.assertEqual(self.gold_of("+97699112233"), (530, 50))
        self.assertEqual(self.gold_of("+97688112233"), (410, 0))

    def test_guest_who_never_connected_gets_the_fee_back(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)
        self.assertEqual(self.gold_of("+97688112233"), (480, 50))
        # Хост баталгаажуулаагүй: тулаан тоологдохгүй
        self.assertEqual(self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest, status=409)["error"],
                         "not_battle")
        # Тоглоомоор холбогдож чадаагүй тул гарвал төлбөр, барьцаа бүтэн буцна, өрөө дараагийн зочинд нээлттэй
        account = self.post(f"rooms/{room_id}/leave/", {}, guest)["account"]
        self.assertEqual((account["gold"], account["gold_locked"]), (500, 0))
        self.assertIsNone(Room.objects.get().guest)
        self.assertEqual(GoldTransaction.objects.filter(reason=GoldTransaction.ROOM_REFUND).count(), 1)

    def test_unconfirmed_guest_expires_with_refund(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)
        Room.objects.filter(pk=room_id).update(guest_joined=timezone.now() - timedelta(minutes=4))
        third = self.register(phone="95112233", device="device-cccc-3333", name="Сүх")["token"]
        self.post("rooms/join/", {"protocol": 13}, third)  # (хайлт хуучирсныг цэвэрлэнэ: гацсан зочин гарч, шинэ зочин орно)
        self.assertEqual(self.gold_of("+97688112233"), (500, 0))
        self.assertEqual(Room.objects.get().guest.name, "Сүх")

    def test_only_same_game_version_is_matched(self):
        host, guest = self.two_players()
        room = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]
        self.assertEqual(self.post("rooms/join/", {"protocol": 11}, guest, status=426)["error"], "update_required")
        self.assertEqual(self.post("rooms/coop/", {"code": room["code"], "protocol": 11}, guest, status=409)["error"],
                         "version_mismatch")
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, guest)["room"]["id"], room["id"])

    def test_undecided_report_voids_and_repeated_disputes_block_battles(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)
        voided = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "none"}, guest)
        self.assertEqual((voided["settled"], voided["result"]), (True, "void"))
        # Шийдэгдээгүй (тэнцээ, дэлгэц зөрсөн): хүчингүй, гэхдээ маргаан биш (тоологдохгүй)
        self.assertEqual(Event.objects.filter(kind="battle_disputed").count(), 0)
        self.assertEqual(Event.objects.filter(kind="battle_void").count(), 2)  # хоёр талд
        for battle in (2, 3, 4):
            self.post("rooms/join/", {"protocol": 13}, guest)
            self.accept(room_id, host)
            self.post(f"rooms/{room_id}/result/", {"battle": battle, "winner": "host"}, host)
            self.post(f"rooms/{room_id}/result/", {"battle": battle, "winner": "guest"}, guest)
        self.assertEqual(Event.objects.filter(kind="battle_disputed").count(), 6)
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, guest, status=403)["error"], "disputes")
        self.assertFalse(self.post("provinces/start/", {"province": 3, "protocol": 13}, guest)["room"]["pvp"])

    def test_disconnect_conflicts_are_not_disputes(self):
        # Сүлжээ тасарч хоёр тал хоёулаа нөгөөгийнхөө багийг AI-аар тулалдуулж ялсан: хүчингүй, маргаанд тоологдохгүй
        host, guest, room_id = self.battle()
        for battle in (1, 2, 3):
            if battle > 1:
                self.post("rooms/join/", {"protocol": 13}, guest)
                self.accept(room_id, host)
            self.post(f"rooms/{room_id}/result/", {"battle": battle, "winner": "host", "absent": True}, host)
            voided = self.post(f"rooms/{room_id}/result/", {"battle": battle, "winner": "guest", "absent": True}, guest)
            self.assertEqual(voided["result"], "void")
        self.assertEqual(Event.objects.filter(kind="battle_disputed").count(), 0)
        self.assertEqual(self.post("rooms/join/", {"protocol": 13}, guest)["room"]["id"], room_id)  # хаагдаагүй
        self.assertEqual(self.gold_of("+97688112233")[1], 50)  # (шинэ тулааны барьцаа л)

    def test_wrong_friend_codes_are_rate_limited(self):
        host, guest = self.two_players()
        self.post("provinces/start/", {"province": 7, "protocol": 13}, host)
        for _ in range(10):
            self.post("rooms/coop/", {"code": "000000"}, guest, status=404)
        self.assertEqual(self.post("rooms/coop/", {"code": "000000"}, guest, status=429)["error"], "too_many_attempts")

    def test_stage_bonus_needs_real_progress(self):
        host = self.register()["token"]
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        Room.objects.filter(pk=room_id).update(created=timezone.now() - timedelta(minutes=30))
        self.post(f"rooms/{room_id}/stage/", {"stage": "1 0 0 0 0", "recruits": 0}, host)
        self.post(f"rooms/{room_id}/stage/", {"stage": "1 0 0 0 0", "recruits": 0}, host)  # өөрчлөгдөөгүй: тоологдохгүй
        account = self.post(f"rooms/{room_id}/complete/", {}, host)["account"]
        self.assertEqual(account["gold"], 480)  # 500 - 20, шагналгүй (барьцаа буцсан)

    def test_same_network_players_battle_without_gold(self):
        # Нэг гэрийн роутер (ижил IP): тулалдаж болно, гэхдээ ялагдагчийн барьцаа ялагчид очихгүй (зүгээр суллагдана)
        settings = GameSettings.load()
        settings.block_same_ip = True
        settings.save()
        host, guest, room_id = self.battle()
        for token in (host, guest):
            self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, token)
        self.assertEqual(self.gold_of("+97699112233"), (480, 50))  # (500 - 20, барьцаа өрөөнд)
        self.assertEqual(self.gold_of("+97688112233"), (480, 0))   # барьцаа шатаагүй
        self.assertTrue(Event.objects.get(kind="battle_won").data["same_network"])

    def test_host_sees_guest_soldiers_from_the_server(self):
        host, guest = self.two_players()
        Player.objects.filter(phone="+97688112233").update(soldiers=2, health_level=3)
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)
        state = self.accept(room_id, host)
        self.assertEqual((state["guest_soldiers"], state["guest_health_level"]), (2, 3))

    def test_stage_cannot_be_completed_during_a_battle(self):
        # Ялагдах гэж буй хост сүүлийн даалгаврыг дуусгаж өрөөг хааж тулааныг хүчингүй болгож чадахгүй
        host, guest, room_id = self.battle()
        Room.objects.filter(pk=room_id).update(created=timezone.now() - timedelta(minutes=30))
        self.played(room_id, host)
        self.assertEqual(self.post(f"rooms/{room_id}/complete/", {}, host, status=409)["error"], "battle_active")
        for token in (host, guest):
            self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, token)
        # Зочин ялж шинэ хост боллоо: тэр үеийг дуусгана
        account = self.post(f"rooms/{room_id}/complete/", {}, guest)["account"]
        self.assertEqual(account["gold_locked"], 0)

    def test_guest_who_never_arrived_is_refunded_when_the_stage_ends(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)  # (хост түүнийг тоглоомд хүлээж аваагүй)
        Room.objects.filter(pk=room_id).update(created=timezone.now() - timedelta(minutes=30))
        self.played(room_id, host)
        self.post(f"rooms/{room_id}/complete/", {}, host)
        self.assertEqual(self.gold_of("+97688112233"), (500, 0))  # төлбөр, барьцаа бүтэн буцсан

    def test_host_reports_guest_soldier_losses(self):
        host, guest = self.two_players()
        Player.objects.filter(phone="+97688112233").update(soldiers=3)
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)
        self.accept(room_id, host)
        self.post(f"rooms/{room_id}/guest-lost/", {"battle": 1, "lost": 2}, host)
        self.post(f"rooms/{room_id}/guest-lost/", {"battle": 1, "lost": 2}, host)  # давтан: дахин хасахгүй
        self.post(f"rooms/{room_id}/guest-lost/", {"battle": 1, "lost": 1}, host)  # хуучирсан: өөрчлөхгүй
        self.post("soldiers/lost/", {"soldiers": 1}, guest)  # зочны өөрийн мэдэгдэл: давхар хасахгүй
        guest_player = Player.objects.get(phone="+97688112233")
        self.assertEqual((guest_player.soldiers, guest_player.soldiers_lost), (1, 2))
        self.assertEqual(self.post(f"rooms/{room_id}/guest-lost/", {"battle": 1, "lost": 1}, guest, status=403)["error"],
                         "not_host")

    def test_stale_rooms_release_stakes_without_a_room_search(self):
        host = self.register()["token"]
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        Room.objects.filter(pk=room_id).update(host_seen=timezone.now() - timedelta(minutes=11))
        logic._last_expire = 0.0
        account = self.get("account/", host)["account"]
        self.assertEqual(Room.objects.get(pk=room_id).status, Room.CLOSED)
        self.assertEqual(account["gold_locked"], 0)

    def test_client_ip_header(self):
        from django.test import RequestFactory, override_settings
        request = RequestFactory().get("/", HTTP_CF_CONNECTING_IP="203.0.113.7", HTTP_X_FORWARDED_FOR="172.69.1.1",
                                       REMOTE_ADDR="127.0.0.1")
        self.assertEqual(logic.request_ip(request), "172.69.1.1")
        with override_settings(CLIENT_IP_HEADER="HTTP_CF_CONNECTING_IP"):
            self.assertEqual(logic.request_ip(request), "203.0.113.7")
        with override_settings(CLIENT_IP_HEADER="HTTP_CF_CONNECTING_IP"):
            bad = RequestFactory().get("/", HTTP_CF_CONNECTING_IP="not-an-ip", REMOTE_ADDR="127.0.0.1")
            self.assertEqual(logic.request_ip(bad), "127.0.0.1")

    def test_guest_who_left_cannot_escape_by_timeout(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/leave/", {}, guest)
        Room.objects.filter(pk=room_id).update(guest_seen=timezone.now() - timedelta(minutes=30))
        logic.expire_rooms()
        self.assertEqual(Player.objects.get(phone="+97688112233").locked_gold, 50)  # үр дүнг хүлээнэ
        for token in (host,):
            self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, token)
        Room.objects.filter(pk=room_id).update(report_at=timezone.now() - timedelta(seconds=61))
        self.assertTrue(self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)["settled"])
        self.assertEqual(self.gold_of("+97688112233"), (430, 0))

    def test_room_phases(self):
        host, guest, room_id = self.battle()
        self.assertEqual(logic.room_phase(Room.objects.get()), "battle")
        for token in (host, guest):
            self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, token)
        self.assertEqual(logic.room_phase(Room.objects.get()), "mission")
        self.post(f"rooms/{room_id}/leave/", {}, host)
        self.assertEqual(logic.room_phase(Room.objects.get()), "closed")

    def test_bad_stage_and_results(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/stage/", {"stage": "1; drop", "recruits": 0}, host, status=400)
        self.post(f"rooms/{room_id}/stage/", {"stage": "1", "recruits": 0}, guest, status=403)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "nobody"}, host, status=400)
        self.post(f"rooms/{room_id}/result/", {"battle": 9, "winner": "host"}, host, status=410)

    def test_game_buys_gold_in_app(self):
        token = self.register()["token"]
        account = self.get("account/", token)["account"]
        self.assertEqual([p["price"] for p in account["gold_packs"]], [3000, 5000, 10000])
        invoice = self.post("invoices/", {"kind": "gold", "pack": 0, "platform": "android"}, token)["invoice"]
        self.assertEqual((invoice["amount"], invoice["gold"]), (3000, 300))
        self.pay(invoice["id"])
        self.assertEqual(self.get(f"invoices/{invoice['id']}/", token)["account"]["gold"], 800)

    # ---- Gold худалдах (вэб)
    def test_web_topup_buys_gold_once(self):
        self.register()
        login = self.post("web/login/", {"phone": "99112233", "password": "secret1"})
        token = login["token"]
        self.assertEqual(login["packs"][0], {"index": 0, "gold": 300, "price": 3000})
        # Вэб нэвтрэлт тоглоомын нэвтрэлтийг гаргахгүй
        self.assertEqual(AuthToken.objects.filter(revoked=False).count(), 2)
        invoice = self.post("invoices/", {"kind": "gold", "pack": 1}, token)["invoice"]
        self.assertEqual((invoice["amount"], invoice["gold"]), (5000, 550))
        self.pay(invoice["id"])
        key = Invoice.objects.get(pk=invoice["id"]).callback_key
        self.client.get(f"/api/qpay/callback/{invoice['id']}/{key}/")  # давхар мэдэгдэл
        self.assertEqual(self.get(f"invoices/{invoice['id']}/", token)["account"]["gold"], 1050)
        self.assertEqual(GoldTransaction.objects.filter(reason="purchase").count(), 1)
        # Вэбээс зөвхөн Gold
        self.post("invoices/", {"kind": "device"}, token, status=403)
        self.post("shop/buy/", {"item": "damage", "level": 0}, token, status=402)
        self.post("provinces/start/", {"province": 1}, token, status=402)
        self.post("invoices/", {"kind": "gold", "pack": 7}, token, status=400)
        self.assertEqual(self.client.get("/api/topup/").status_code, 200)

    # ---- төхөөрөмж
    def test_second_device_is_free_third_needs_payment(self):
        self.register()
        second = self.post("login/", {"phone": "99112233", "password": "secret1", "device_id": "device-two-2222"})
        self.assertTrue(second["account"]["device_ok"])  # 2 дахь төхөөрөмж үнэгүй бүртгэгдэнэ
        self.assertEqual(Device.objects.filter(active=True).count(), 2)
        login = self.post("login/", {"phone": "99112233", "password": "secret1", "device_id": "device-new-3333"})
        token = login["token"]
        self.assertFalse(login["account"]["device_ok"])
        self.assertEqual(self.post("provinces/claim/", {"province": 1}, token, status=402)["error"], "device_change_required")
        self.assertEqual(self.post("shop/buy/", {"item": "damage", "level": 0}, token, status=402)["error"],
                         "device_change_required")
        invoice = self.post("invoices/", {"kind": "device"}, token)["invoice"]
        self.assertEqual(invoice["amount"], 5000)
        self.pay(invoice["id"])
        self.assertTrue(self.get("account/", token)["account"]["device_ok"])
        # Хамгийн эрт бүртгэгдсэн нь чөлөөлөгдөж, 2 төхөөрөмж хэвээр
        self.assertEqual(Device.objects.get(device_id="device-aaaa-1111").active, False)
        self.assertEqual(Device.objects.filter(active=True).count(), 2)
        # Хуучин төхөөрөмжөөс шинэ данс нээж болохгүй
        self.assertEqual(self.post("register/start/", {"name": "Дорж", "phone": "88112233", "password": "secret1",
                                                       "device_id": "device-aaaa-1111"}, status=409)["error"], "device_taken")

    def test_one_session_at_a_time(self):
        first = self.register()["token"]
        second = self.post("login/", {"phone": "99112233", "password": "secret1", "device_id": "device-two-2222"})["token"]
        # Өмнөх төхөөрөмж гарна: нэг удаа "өөр төхөөрөмж дээр нэвтэрсэн", дараа нь ердийн 401
        self.assertEqual(self.get("account/", first, status=401)["error"], "session_replaced")
        self.assertEqual(self.get("account/", first, status=401)["error"], "auth")
        self.get("account/", second)
        self.assertEqual(AuthToken.objects.filter(revoked=False).count(), 1)

    def test_cannot_login_on_another_accounts_device(self):
        self.register()
        self.register(phone="88112233", device="device-bbbb-2222", name="Дорж")
        self.assertEqual(self.post("login/", {"phone": "99112233", "password": "secret1", "device_id": "device-bbbb-2222"},
                                   status=409)["error"], "device_taken")

    def test_register_without_sms(self):
        result = self.post("register/", {"name": "Тэмүүлэн", "phone": "95112233", "password": "secret1",
                                         "device_id": "device-dddd-4444", "platform": "android"})
        self.assertEqual((result["account"]["gold"], result["account"]["phone"]), (500, "+976****2233"))
        self.get("account/", result["token"])
        self.assertFalse(Verification.objects.exists())  # мессеж илгээгээгүй
        self.assertEqual(self.post("register/", {"name": "Өөр", "phone": "95112233", "password": "secret1",
                                                 "device_id": "device-eeee-5555"}, status=409)["error"], "phone_claimable")
        self.assertEqual(self.post("register/", {"name": "Өөр", "phone": "96112233", "password": "secret1",
                                                 "device_id": "device-dddd-4444"}, status=409)["error"], "device_taken")
        self.post("login/", {"phone": "95112233", "password": "secret1", "device_id": "device-dddd-4444"})
        # Нэг сүлжээнээс хоногт тохиргооноос олон данс нээхгүй
        GameSettings.objects.filter(pk=GameSettings.load().pk).update(register_per_ip_day=5)
        for k in range(4):
            self.post("register/", {"name": f"Тоглогч{k}", "phone": f"9{k}332211", "password": "secret1",
                                    "device_id": f"device-ip-{k}000"})
        self.assertEqual(self.post("register/", {"name": "Тав", "phone": "99887700", "password": "secret1",
                                                 "device_id": "device-ip-9000"}, status=429)["error"], "too_many_accounts")

    # ---- утас баталгаажуулалт
    def quick_player(self, phone="95112233", device="device-dddd-4444", name="Тэмүүлэн"):
        """SMS-гүй бүртгэл (баталгаажаагүй)."""
        return self.post("register/", {"name": name, "phone": phone, "password": "secret1", "device_id": device})["token"]

    def verify_phone(self, token):
        start = self.post("phone/verify/start/", {}, token)
        self.assertEqual(self.post("phone/verify/confirm/", {"verification_id": start["verification_id"]}, token,
                                   status=409)["error"], "not_verified")
        self.sms_sent(start["verification_id"])
        return self.post("phone/verify/confirm/", {"verification_id": start["verification_id"]}, token)["account"]

    def test_verifying_the_phone_gives_the_bonus_once(self):
        GameSettings.objects.filter(pk=GameSettings.load().pk).update(verify_bonus=100)
        token = self.quick_player()
        account = self.get("account/", token)["account"]
        self.assertEqual((account["verified"], account["gold"], account["verify_bonus"]), (False, 500, 100))
        account = self.verify_phone(token)
        self.assertEqual((account["verified"], account["gold"]), (True, 600))
        self.assertEqual(self.post("phone/verify/start/", {}, token, status=409)["error"], "already_verified")
        self.assertEqual(Player.objects.get().bonus_gold, 600)  # урамшуулал: татагдахгүй

    def test_unverified_player_gains_nothing_but_still_pays(self):
        host = self.quick_player()
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]  # төлбөр, барьцаа хасагдана
        self.assertEqual(self.gold_of("+97695112233"), (480, 50))
        Room.objects.filter(pk=room_id).update(created=timezone.now() - timedelta(minutes=30))
        self.played(room_id, host)
        result = self.post(f"rooms/{room_id}/complete/", {}, host)
        self.assertEqual((result["account"]["gold"], result["missed"]), (480, 40))  # +40 олгоогүй
        self.assertTrue(Event.objects.filter(kind="reward_skipped").exists())

    def battle_between(self, host, guest):
        room_id = self.post("provinces/start/", {"province": 7, "protocol": 13}, host)["room"]["id"]
        self.post("rooms/join/", {"protocol": 13}, guest)
        self.accept(room_id, host)
        return room_id

    def test_unverified_loser_burns_the_stake(self):
        winner = self.register()["token"]  # SMS-ээр: баталгаажсан
        loser = self.quick_player()
        room_id = self.battle_between(winner, loser)
        for token in (winner, loser):
            self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, token)
        self.assertEqual(self.gold_of("+97695112233"), (430, 0))  # 500 - 20 - 50
        self.assertEqual(self.gold_of("+97699112233"), (480, 50))  # хожил ирээгүй: шатсан

    def test_unverified_winner_gets_nothing_and_verified_loser_keeps_the_stake(self):
        loser = self.register()["token"]
        winner = self.quick_player()
        room_id = self.battle_between(loser, winner)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, loser)
        result = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, winner)
        self.assertEqual((result["result"], result["missed"]), ("won", 50))
        self.assertEqual(self.gold_of("+97699112233"), (480, 0))   # барьцаа буцсан
        self.assertEqual(self.gold_of("+97695112233"), (480, 50))  # шинэ хост, хожилгүй

    def test_real_owner_takes_back_a_squatted_number(self):
        squatter = self.quick_player(phone="99112233", device="device-squat-111")
        self.assertEqual(self.post("register/", {"name": "Бат", "phone": "99112233", "password": "secret1",
                                                 "device_id": "device-aaaa-1111"}, status=409)["error"], "phone_claimable")
        owner = self.register()  # SMS-ээр баталгаажуулж бүртгүүлнэ
        self.assertTrue(owner["account"]["verified"])
        self.assertEqual(self.get("account/", squatter, status=401)["error"], "auth")
        self.assertIsNone(Player.objects.get(name="Тэмүүлэн").phone)

    def test_unverified_cannot_cash_out(self):
        token = self.quick_player()
        GameSettings.objects.filter(pk=GameSettings.load().pk).update(cashout_enabled=True)
        self.assertEqual(self.post("cashouts/", {"gold": 100, "bank": "Хаан", "account": "123", "holder": "Т"}, token,
                                   status=403)["error"], "phone_required")

    def test_old_game_versions_cannot_battle(self):
        host, guest = self.two_players()
        room = self.post("provinces/start/", {"province": 7}, host)["room"]  # (хувилбаргүй = хуучин)
        self.assertFalse(room["pvp"])
        self.assertEqual(self.post("rooms/join/", {}, guest, status=426)["error"], "update_required")

    def test_bad_login(self):
        self.register()
        self.assertEqual(self.post("login/", {"phone": "99112233", "password": "wrong!!", "device_id": "device-aaaa-1111"},
                                   status=401)["error"], "bad_login")

    # ---- лог, хадгалалт, нууц үг
    def test_events_and_progress(self):
        token = self.register()["token"]
        stored = self.post("events/", {"events": [{"kind": "province_liberated", "data": {"province": 9, "minutes": 46}},
                                                  {"kind": "BAD KIND", "data": {}}]}, token)["stored"]
        self.assertEqual(stored, 1)
        self.assertTrue(Event.objects.filter(kind="game_province_liberated").exists())
        response = self.client.put("/api/progress/", {"progress": "coins 120\n"}, content_type="application/json",
                                   HTTP_AUTHORIZATION=f"Bearer {token}")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(self.get("progress/", token)["progress"], "coins 120\n")

    def test_password_reset(self):
        token = self.register()["token"]
        start = self.post("password/reset/start/", {"phone": "99112233"})
        self.sms_sent(start["verification_id"])
        self.post("password/reset/confirm/", {"verification_id": start["verification_id"], "password": "newpass1"})
        self.get("account/", token, status=401)  # хуучин нэвтрэлт хүчингүй
        self.post("login/", {"phone": "99112233", "password": "newpass1", "device_id": "device-aaaa-1111"})


@override_settings(DEBUG=True, QPAY_MODE="mock", SMS_MODE="console")
class ManageApiTests(FlowTests.__base__):
    """Админ самбарын API (/api/manage/): staff нэвтрэлт, тоглогч засах, тохиргоо."""

    def setUp(self):
        from django.contrib.auth import get_user_model

        users = get_user_model()
        users.objects.create_superuser("boss", "", "pass12345")
        users.objects.create_user("plain", "", "pass12345")  # staff биш
        self.player = Player.objects.create(name="Бат", phone="+97699112233", password="x")

    def call(self, method, path, data=None, token=None, status=200):
        headers = {"HTTP_AUTHORIZATION": f"Bearer {token}"} if token else {}
        response = getattr(self.client, method)(f"/api/manage/{path}", data or {}, content_type="application/json", **headers)
        self.assertEqual(response.status_code, status, response.content)
        return response.json()

    def login(self):
        return self.call("post", "login/", {"username": "boss", "password": "pass12345"})["token"]

    def test_cashout_paid_or_rejected(self):
        token = self.login()
        player = self.player
        player.gold, player.won_gold = 60000, 60000
        player.save()
        verification = Verification.objects.create(phone=player.phone, purpose="cashout", expires=timezone.now() + timedelta(minutes=5))
        verifying = Cashout.objects.create(player=player, gold=20000, amount=20000, rate=1, bank="Хаан банк", account="123456",
                                           holder="Бат", verification=verification)
        pending = Cashout.objects.create(player=player, gold=20000, amount=20000, rate=1, bank="Голомт банк", account="654321",
                                         holder="Бат", status="pending", verification=verification)
        listing = self.call("get", "cashouts/?status=pending", token=token)
        self.assertEqual((listing["total"], listing["pending_total"], listing["items"][0]["account"]), (1, 20000, "654321"))
        self.call("post", f"cashouts/{verifying.pk}/paid/", {}, token=token, status=409)  # баталгаажаагүй
        self.call("post", f"cashouts/{pending.pk}/reject/", {"note": ""}, token=token, status=400)
        paid = self.call("post", f"cashouts/{pending.pk}/paid/", {"note": "TX-1"}, token=token)["cashout"]
        self.assertEqual((paid["status"], paid["handled_by"], paid["note"]), ("paid", "boss", "TX-1"))
        rejected = self.call("post", f"cashouts/{verifying.pk}/reject/", {"note": "Данс буруу"}, token=token)["cashout"]
        self.assertEqual(rejected["status"], "rejected")
        player.refresh_from_db()
        self.assertEqual((player.gold, player.won_gold), (80000, 80000))  # татгалзсаны Gold буцсан
        self.assertTrue(Event.objects.filter(kind="admin_cashout_paid").exists())

    def test_only_staff_can_log_in(self):
        self.call("post", "login/", {"username": "plain", "password": "pass12345"}, status=401)
        self.call("post", "login/", {"username": "boss", "password": "wrong"}, status=401)
        self.call("get", "dashboard/", status=401)
        self.call("get", "dashboard/", token="garbage", status=401)
        token = self.login()
        dashboard = self.call("get", "dashboard/", token=token)
        self.assertEqual(dashboard["totals"]["players"], 1)
        self.assertEqual(len(dashboard["series"]), 30)

    def test_dashboard_counts_online_players(self):
        Device.objects.create(device_id="device-aaaa-1111", player=self.player, platform="android")
        AuthToken.objects.create(key="t1", player=self.player, device_id="device-aaaa-1111")
        AuthToken.objects.create(key="t2", player=self.player, device_id=logic.WEB_DEVICE)  # вэбийн цэнэглэх хуудас
        other = Player.objects.create(name="Дорж", phone="+97688112233", password="x")
        AuthToken.objects.create(key="t3", player=other, device_id="device-bbbb-2222")
        AuthToken.objects.filter(key="t3").update(last_used=timezone.now() - timedelta(minutes=10))  # 10 минутын өмнө
        Room.objects.create(province=3, host=self.player, guest=other, host_seen=timezone.now())
        dashboard = self.call("get", "dashboard/", token=self.login())
        totals = dashboard["totals"]
        self.assertEqual((totals["online"], totals["active_1h"], totals["rooms_open"], totals["battles_live"]), (1, 2, 1, 1))
        self.assertEqual(dashboard["online_platforms"], {"android": 1})

    def test_edit_player_and_audit(self):
        token = self.login()
        result = self.call("patch", f"players/{self.player.pk}/", {"credits": 5, "blocked": True}, token=token)
        self.assertEqual(result["player"]["credits"], 5)
        self.assertTrue(result["player"]["blocked"])
        result = self.call("post", f"players/{self.player.pk}/province/", {"province": UB}, token=token)
        self.assertEqual(result["player"]["unlocked"][0]["source"], "admin")
        self.assertTrue(Event.objects.filter(kind="admin_player_updated", data__admin="boss").exists())
        listing = self.call("get", "players/?q=99112", token=token)
        self.assertEqual(listing["total"], 1)

    def test_admin_sets_gold_through_ledger(self):
        token = self.login()
        result = self.call("patch", f"players/{self.player.pk}/", {"gold": 750}, token=token)
        self.assertEqual(result["player"]["gold"], 750)
        entry = GoldTransaction.objects.get(player=self.player)
        self.assertEqual((entry.amount, entry.balance, entry.reason, entry.data["staff"]), (250, 750, "admin", "boss"))
        self.call("patch", f"players/{self.player.pk}/", {"gold": -5}, token=token, status=400)
        self.assertEqual(self.call("put", "settings/", {"price_soldier": 150}, token=token)["settings"]["price_soldier"], 150)
        changed = self.call("put", "settings/", {"skin_price_iron": 80, "coop_reward": 25, "coop_daily_limit": 3}, token=token)["settings"]
        self.assertEqual((changed["skin_price_iron"], changed["coop_reward"], changed["coop_daily_limit"]), (80, 25, 3))
        self.call("put", "settings/", {"coop_reward": -1}, token=token, status=400)
        packs = self.call("put", "settings/", {"gold_packs": [{"gold": 100, "price": 1000}]}, token=token)["settings"]["gold_packs"]
        self.assertEqual(packs, [{"gold": 100, "price": 1000}])
        self.call("put", "settings/", {"gold_packs": [{"gold": 0, "price": 1000}]}, token=token, status=400)
        self.assertEqual(self.call("get", "rooms/", token=token)["total"], 0)

    def test_settings_and_mark_paid(self):
        token = self.login()
        self.assertEqual(self.call("put", "settings/", {"pack_price": 4000}, token=token)["settings"]["pack_price"], 4000)
        self.assertEqual(GameSettings.load().pack_price, 4000)
        invoice = Invoice.objects.create(player=self.player, kind=Invoice.PACK, amount=4000, credits=3)
        self.call("post", f"invoices/{invoice.pk}/mark-paid/", {}, token=token)
        self.player.refresh_from_db()
        self.assertEqual(self.player.credits, 3)
