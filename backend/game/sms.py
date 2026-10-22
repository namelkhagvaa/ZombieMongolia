"""Утасны дугаар баталгаажуулах: тоглогч өөрийн утаснаас богино дугаар руу код илгээнэ (урвуу SMS).

Тоглогч код оруулахгүй: сервер 6 оронтой код үүсгэж "144773 руу 482916 гэж илгээнэ үү" гэсэн заавар өгнө,
тоглогч мессеж илгээсний дараа үйлчилгээ тэр дугаараас ирснийг баталгаажуулна (дугаар тоглогчийнх гэдгийг
нотолно). Нэг SMS-ийн төлбөрийг (150₮) тоглогч өөрөө төлнө.

SMS_MODE:
  console   зөвхөн автомат тест: мессеж илгээгдэхгүй. Серверт (DEBUG=0) ашиглахыг хориглоно
  verifymn  https://api.verify.mn (VERIFYMN_API_URL, VERIFYMN_API_KEY .env-д)
"""
import logging
import secrets
from datetime import datetime

import requests
from django.conf import settings

log = logging.getLogger("game")


class SmsError(Exception):
    pass


def _local_number(phone):
    """+97699112233 -> 99112233"""
    return phone[4:] if phone.startswith("+976") else phone


class ConsoleVerifier:
    def start(self, verification):
        code = f"{secrets.randbelow(1000000):06d}"
        verification.provider_ref = f"console-{code}"
        log.warning("SMS (console): %s утас 144773 руу %s илгээх ёстой", verification.phone, code)
        return {"shortcode": "144773", "text": code, "sms_uri": f"sms:144773?body={code}",
                "instruction": f"(Хөгжүүлэлт) 144773 дугаарт \"{code}\" гэж SMS илгээнэ үү"}

    def is_verified(self, verification):
        return bool(verification.payload.get("dev_verified"))


class VerifyMn:
    def __init__(self):
        self.base = settings.VERIFYMN_API_URL.rstrip("/")

    def start(self, verification):
        code = f"{secrets.randbelow(1000000):06d}"
        try:
            response = requests.post(f"{self.base}/sessions", json={"phone": _local_number(verification.phone), "text": code},
                                     headers={"Authorization": f"Bearer {settings.VERIFYMN_API_KEY}"}, timeout=15)
        except requests.RequestException as error:
            raise SmsError(str(error))
        if response.status_code not in (200, 201):
            raise SmsError(f"sessions {response.status_code}: {response.text[:200]}")
        data = response.json()
        verification.provider_ref = data["sessionId"]
        expires = data.get("expiresAt")
        if expires:
            verification.expires = min(verification.expires, datetime.fromisoformat(expires.replace("Z", "+00:00")))
        return {"shortcode": data.get("shortcode", "144773"), "text": data.get("text", code),
                "sms_uri": data.get("smsUri", ""), "instruction": data.get("displayInstruction", "")}

    def is_verified(self, verification):
        try:
            response = requests.get(f"{self.base}/sessions/{verification.provider_ref}", timeout=15)
        except requests.RequestException as error:
            raise SmsError(str(error))
        if response.status_code != 200:
            raise SmsError(f"session {response.status_code}: {response.text[:200]}")
        return response.json().get("sessionStatus") == "VERIFIED"


def verifier():
    if settings.SMS_MODE == "verifymn":
        return VerifyMn()
    return ConsoleVerifier()
