from django.test import TestCase, override_settings

from . import logic, qpay
from .models import AuthToken, Device, Event, GameSettings, GoldTransaction, Invoice, Player, Verification

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

    def test_settings_and_mark_paid(self):
        token = self.login()
        self.assertEqual(self.call("put", "settings/", {"pack_price": 4000}, token=token)["settings"]["pack_price"], 4000)
        self.assertEqual(GameSettings.load().pack_price, 4000)
        invoice = Invoice.objects.create(player=self.player, kind=Invoice.PACK, amount=4000, credits=3)
        self.call("post", f"invoices/{invoice.pk}/mark-paid/", {}, token=token)
        self.player.refresh_from_db()
        self.assertEqual(self.player.credits, 3)
