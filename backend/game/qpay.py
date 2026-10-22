"""QPay merchant API v2: нэхэмжлэх үүсгэх, төлөгдсөн эсэхийг шалгах.

QPAY_MODE:
  mock        зөвхөн автомат тест (manage.py test): бодит төлбөргүй. Серверт (DEBUG=0) ашиглахыг хориглоно
  sandbox     https://merchant-sandbox.qpay.mn
  production  https://merchant.qpay.mn
Нэвтрэх нэр, нууц үг, нэхэмжлэхийн кодыг backend/.env-д (QPAY_USERNAME, QPAY_PASSWORD, QPAY_INVOICE_CODE).
"""
import logging
import threading
import time

import requests
from django.conf import settings

log = logging.getLogger("game")


class QPayError(Exception):
    pass


class QPayClient:
    def __init__(self):
        self.base = settings.QPAY_BASE_URL.rstrip("/")
        self._token = None
        self._token_expires = 0.0
        self._lock = threading.Lock()

    def _access_token(self):
        with self._lock:
            if self._token and time.time() < self._token_expires - 60:
                return self._token
            response = requests.post(f"{self.base}/v2/auth/token",
                                     auth=(settings.QPAY_USERNAME, settings.QPAY_PASSWORD), timeout=15)
            if response.status_code != 200:
                raise QPayError(f"auth {response.status_code}: {response.text[:200]}")
            data = response.json()
            self._token = data["access_token"]
            expires = float(data.get("expires_in", 3600))
            # expires_in: зарим хувилбарт хугацаа (сек), заримд нь дуусах агшин (unix)
            self._token_expires = expires if expires > 1e9 else time.time() + expires
            return self._token

    def _post(self, path, payload):
        response = requests.post(f"{self.base}{path}", json=payload, timeout=20,
                                 headers={"Authorization": f"Bearer {self._access_token()}"})
        if response.status_code not in (200, 201):
            raise QPayError(f"{path} {response.status_code}: {response.text[:300]}")
        return response.json()

    def create_invoice(self, invoice, description):
        data = self._post("/v2/invoice", {
            "invoice_code": settings.QPAY_INVOICE_CODE,
            "sender_invoice_no": str(invoice.pk),
            "invoice_receiver_code": invoice.player.phone,
            "invoice_description": description,
            "amount": invoice.amount,
            "callback_url": f"{settings.PUBLIC_BASE_URL.rstrip('/')}/api/qpay/callback/{invoice.pk}/{invoice.callback_key}/",
        })
        return {
            "invoice_id": data.get("invoice_id", ""),
            "qr_text": data.get("qr_text", ""),
            "qr_image": data.get("qr_image", ""),  # base64 PNG
            "short_url": data.get("qPay_shortUrl", ""),
            "urls": [{"name": u.get("name", ""), "link": u.get("link", "")} for u in data.get("urls", [])],
        }

    def check_paid(self, invoice):
        """(төлөгдсөн эсэх, гүйлгээний дугаар): нэхэмжлэхийн дүнгээс багагүй төлбөр орсон бол төлөгдсөн."""
        data = self._post("/v2/payment/check", {
            "object_type": "INVOICE",
            "object_id": invoice.qpay_invoice_id,
            "offset": {"page_number": 1, "page_limit": 100},
        })
        paid = 0.0
        payment_id = ""
        for row in data.get("rows", []):
            if row.get("payment_status") == "PAID":
                paid += float(row.get("payment_amount", 0) or 0)
                payment_id = payment_id or str(row.get("payment_id", ""))
        return paid >= invoice.amount, payment_id


def _mock_qr_png(text):
    """Туршилтын QR зураг (base64 PNG): segno суусан бол, үгүй бол хоосон (тоглоом "QR (туршилт)" гэж бичнэ)."""
    try:
        import base64
        import io

        import segno
    except ImportError:
        return ""
    buffer = io.BytesIO()
    segno.make(text, error="m").save(buffer, kind="png", scale=6, border=2)
    return base64.b64encode(buffer.getvalue()).decode()


class MockQPay:
    """Бодит төлбөргүй: нэхэмжлэх үүсгэгдэнэ, төлбөрийг гараар тэмдэглэнэ (mock_paid)."""

    paid = set()

    def create_invoice(self, invoice, description):
        qr_text = f"MOCK-QPAY|{invoice.pk}|{invoice.amount}"
        return {
            "invoice_id": f"mock-{invoice.pk}",
            "qr_text": qr_text,
            "qr_image": _mock_qr_png(qr_text),
            "short_url": "",
            "urls": [],
        }

    def check_paid(self, invoice):
        return invoice.qpay_invoice_id in self.paid, f"mock-payment-{invoice.pk}" if invoice.qpay_invoice_id in self.paid else ""


_client = None


def client():
    global _client
    if _client is None:
        _client = MockQPay() if settings.QPAY_MODE == "mock" else QPayClient()
    return _client
