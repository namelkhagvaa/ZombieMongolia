import base64
import hashlib
import hmac
import json
from datetime import timedelta

from django.test import TestCase, override_settings
from django.utils import timezone

from . import logic, qpay
from .models import AuthToken, Device, Event, GameSettings, GoldTransaction, Invoice, Player, Room, Verification

UB = 15  # Улаанбаатар


@override_settings(DEBUG=True, QPAY_MODE="mock", SMS_MODE="console")
class FlowTests(TestCase):
    def setUp(self):
        qpay.MockQPay.paid.clear()

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
        player.soldiers = 4
        player.gold = 30
        player.save()
        self.assertEqual(self.post("shop/buy/", {"item": "soldier", "level": 4}, token, status=409)["error"], "max_level")
        # Амь 50 x 1 = 50 Gold > 30
        self.assertEqual(self.post("shop/buy/", {"item": "health", "level": 0}, token, status=402)["error"], "not_enough_gold")
        self.assertEqual(self.post("shop/buy/", {"item": "armor", "level": 0}, token, status=400)["error"], "bad_item")
        self.assertEqual(self.post("shop/buy/", {"item": "damage", "level": "0"}, token, status=400)["error"], "bad_item")
        player.refresh_from_db()
        self.assertEqual((player.gold, player.health_level), (30, 0))

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
    def two_players(self):
        host = self.register()["token"]
        guest = self.register(phone="88112233", device="device-bbbb-2222", name="Дорж")["token"]
        return host, guest

    def test_enough_gold_opens_a_room_otherwise_solo(self):
        host = self.register()["token"]
        result = self.post("provinces/start/", {"province": 3}, host)
        room = result["room"]
        self.assertEqual((room["province"], room["role"]), (3, "host"))
        self.assertEqual((result["account"]["gold"], result["account"]["gold_locked"]), (480, 50))
        # Тасалбар: relay ижил нууц үгээр шалгана
        body, signature = room["ticket"].split(".")
        self.assertEqual(signature, hmac.new(b"dev-only-relay-secret", body.encode(), hashlib.sha256).hexdigest())
        payload = json.loads(base64.urlsafe_b64decode(body + "=" * (-len(body) % 4)))
        self.assertEqual((payload["r"], payload["role"]), (room["id"], "host"))
        # Дахин эхэлбэл өмнөх өрөө (апп унасан г.м.) шийтгэлгүй хаагдаж, шинэ өрөө нээгдэнэ
        result = self.post("provinces/start/", {"province": 4}, host)
        self.assertEqual((result["account"]["gold"], result["account"]["gold_locked"]), (460, 50))
        self.assertEqual(Room.objects.get(pk=room["id"]).closed_reason, "replaced")
        # 70-аас бага Gold: ганцаарчилсан, Gold өөрчлөгдөхгүй
        poor = self.register(phone="88112233", device="device-bbbb-2222", name="Дорж")["token"]
        Player.objects.filter(phone="+97688112233").update(gold=69)
        result = self.post("provinces/start/", {"province": 4}, poor)
        self.assertIsNone(result["room"])
        self.assertEqual((result["account"]["gold"], result["account"]["gold_locked"]), (69, 0))

    def test_locked_gold_cannot_be_spent(self):
        host = self.register()["token"]
        player = Player.objects.get()
        player.gold = 120
        player.save()
        self.post("provinces/start/", {"province": 0}, host)  # 100 үлдэж, 50 нь барьцаанд
        # Цэрэг 120 > 50 (боломжтой)
        self.assertEqual(self.post("shop/buy/", {"item": "soldier", "level": 0}, host, status=402)["error"], "not_enough_gold")
        self.post("shop/buy/", {"item": "magazine", "level": 0}, host)  # 40 <= 50
        player.refresh_from_db()
        self.assertEqual((player.gold, player.locked_gold), (60, 50))

    def test_guest_joins_without_invitation(self):
        host, guest = self.two_players()
        self.assertEqual(self.post("rooms/join/", {}, guest, status=404)["error"], "no_rooms")
        room_id = self.post("provinces/start/", {"province": 7}, host)["room"]["id"]
        self.assertEqual(self.post("rooms/join/", {}, host, status=404)["error"], "no_rooms")  # өөрийн өрөө биш
        joined = self.post("rooms/join/", {}, guest)
        self.assertEqual((joined["room"]["id"], joined["room"]["role"], joined["room"]["host"]), (room_id, "guest", "Бат"))
        self.assertEqual((joined["account"]["gold"], joined["account"]["gold_locked"]), (480, 50))
        # Өрөө дүүрсэн
        third = self.register(phone="95112233", device="device-cccc-3333", name="Сүх")["token"]
        self.assertEqual(self.post("rooms/join/", {}, third, status=404)["error"], "no_rooms")
        # Gold хүрэлцэхгүй
        player = Player.objects.get(phone="+97695112233")
        player.gold = 60
        player.save()
        self.assertEqual(self.post("rooms/join/", {}, third, status=402)["error"], "not_enough_gold")

    def test_host_leaving_burns_the_stake(self):
        host = self.register()["token"]
        room_id = self.post("provinces/start/", {"province": 7}, host)["room"]["id"]
        account = self.post(f"rooms/{room_id}/leave/", {}, host)["account"]
        self.assertEqual((account["gold"], account["gold_locked"]), (430, 0))  # 500 - 20 - 50, хэнд ч очоогүй
        self.assertEqual(self.post(f"rooms/{room_id}/leave/", {}, host, status=410)["error"], "room_closed")
        self.assertEqual(Room.objects.get().closed_reason, "host_left")

    def test_guest_leaving_keeps_the_battle(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7}, host)["room"]["id"]
        self.post("rooms/join/", {}, guest)
        # Тулааны үеэр гарсан зочны багийг AI тулалдуулна: барьцаа үр дүн гартал түгжээтэй, байр сул биш
        self.assertEqual(self.post(f"rooms/{room_id}/leave/", {}, guest)["account"]["gold_locked"], 50)
        third = self.register(phone="95112233", device="device-cccc-3333", name="Сүх")["token"]
        self.assertEqual(self.post("rooms/join/", {}, third, status=404)["error"], "no_rooms")
        self.assertTrue(Room.objects.get().guest_left)

    def test_completing_the_stage_pays_once(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7}, host)["room"]["id"]
        self.assertEqual(self.post(f"rooms/{room_id}/complete/", {}, host, status=409)["error"], "too_fast")
        Room.objects.filter(pk=room_id).update(created=timezone.now() - timedelta(minutes=30))
        self.post("rooms/join/", {}, guest)
        self.assertEqual(self.post(f"rooms/{room_id}/complete/", {}, guest, status=403)["error"], "not_host")
        account = self.post(f"rooms/{room_id}/complete/", {}, host)["account"]
        self.assertEqual((account["gold"], account["gold_locked"]), (520, 0))  # 500 - 20 + 40
        self.assertEqual(self.post(f"rooms/{room_id}/complete/", {}, host, status=410)["error"], "room_closed")
        self.assertEqual(Player.objects.get(phone="+97688112233").locked_gold, 0)

    def test_lost_connection_is_not_a_penalty(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7}, host)["room"]["id"]
        self.post(f"rooms/{room_id}/beat/", {}, host)
        Room.objects.filter(pk=room_id).update(host_seen=timezone.now() - timedelta(minutes=11))
        # Хостын дохио тасарсан: зочин тэр өрөөнд орохгүй, хостын барьцаа шийтгэлгүй буцна
        self.assertEqual(self.post("rooms/join/", {}, guest, status=404)["error"], "no_rooms")
        host_player = Player.objects.get(phone="+97699112233")
        self.assertEqual((host_player.gold, host_player.locked_gold), (480, 0))
        self.assertEqual(Room.objects.get().closed_reason, "timeout")
        self.assertEqual(sum(t.amount for t in host_player.gold_transactions.all()), host_player.gold)

    # ---- тулаан
    def battle(self):
        host, guest = self.two_players()
        room_id = self.post("provinces/start/", {"province": 7}, host)["room"]["id"]
        joined = self.post("rooms/join/", {}, guest)["room"]
        self.assertEqual(joined["battle"], 1)
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
        room = self.post("rooms/join/", {}, third)["room"]
        self.assertEqual((room["id"], room["battle"], room["stage"]), (room_id, 2, "1 3 0 0 0"))

    def test_guest_wins_and_becomes_host(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/stage/", {"stage": "4 15 0 0 0", "recruits": 3}, host)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest)
        result = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, host)
        self.assertEqual((result["settled"], result["winner"]), (True, "guest"))
        # Түрүүлж мэдэгдсэн зочин дахин асуухад шинэ хостын тасалбар, ахиц, ялагдагчийн цэргүүд
        polled = self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest)
        self.assertTrue(polled["settled"])
        self.assertEqual((polled["room"]["role"], polled["room"]["stage"], polled["room"]["recruits"]), ("host", "4 15 0 0 0", 3))
        self.assertEqual(self.gold_of("+97688112233"), (530, 50))  # шинэ хост, барьцаа хэвээр
        self.assertEqual(self.gold_of("+97699112233"), (430, 0))
        self.assertEqual(Room.objects.get().host.name, "Дорж")
        self.post(f"rooms/{room_id}/leave/", {}, host, status=410)  # хуучин хост өрөөнд байхгүй

    def test_disputed_result_pays_nobody(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "host"}, host)
        self.assertTrue(self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "guest"}, guest)["settled"])
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
        self.post("rooms/join/", {}, guest)
        for token in (host, guest):
            self.post(f"rooms/{room_id}/result/", {"battle": 2, "winner": "host"}, token)
        # Хоёр дахь тулаанд Gold шилжээгүй (зөвхөн 20 төлбөр)
        self.assertEqual(self.gold_of("+97699112233"), (530, 50))
        self.assertEqual(self.gold_of("+97688112233"), (410, 0))

    def test_bad_stage_and_results(self):
        host, guest, room_id = self.battle()
        self.post(f"rooms/{room_id}/stage/", {"stage": "1; drop", "recruits": 0}, host, status=400)
        self.post(f"rooms/{room_id}/stage/", {"stage": "1", "recruits": 0}, guest, status=403)
        self.post(f"rooms/{room_id}/result/", {"battle": 1, "winner": "nobody"}, host, status=400)
        self.post(f"rooms/{room_id}/result/", {"battle": 9, "winner": "host"}, host, status=410)

    # ---- Gold худалдах (вэб)
    def test_web_topup_buys_gold_once(self):
        self.register()
        login = self.post("web/login/", {"phone": "99112233", "password": "secret1"})
        token = login["token"]
        self.assertEqual(login["packs"][0], {"index": 0, "gold": 200, "price": 2000})
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

    def test_only_staff_can_log_in(self):
        self.call("post", "login/", {"username": "plain", "password": "pass12345"}, status=401)
        self.call("post", "login/", {"username": "boss", "password": "wrong"}, status=401)
        self.call("get", "dashboard/", status=401)
        self.call("get", "dashboard/", token="garbage", status=401)
        token = self.login()
        dashboard = self.call("get", "dashboard/", token=token)
        self.assertEqual(dashboard["totals"]["players"], 1)
        self.assertEqual(len(dashboard["series"]), 30)

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
