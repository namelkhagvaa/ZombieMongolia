"""Тоглоомын JSON API (/api/...).

Хариу: {"ok": true, ...} эсвэл {"ok": false, "error": "<код>", "message": "<монгол мессеж>"} (HTTP 4xx/5xx).
Нэвтэрсний дараах хүсэлтүүд: толгойд "Authorization: Bearer <token>", "X-Device-Id" шаардлагагүй
(токен үүсгэсэн төхөөрөмжөөр тооцно).
"""
import json
import re
from functools import wraps

from django.http import HttpResponse, JsonResponse
from django.utils import timezone
from django.views.decorators.csrf import csrf_exempt

from . import logic
from .logic import GameError
from .models import AuthToken, Invoice

MAX_PROGRESS = 64 * 1024
MAX_EVENTS = 50


def client_ip(request):
    forwarded = request.META.get("HTTP_X_FORWARDED_FOR", "")
    return forwarded.split(",")[0].strip() if forwarded else request.META.get("REMOTE_ADDR")


def api(methods, auth=True):
    """JSON хүсэлт: аргыг шалгаж, бие хэсгийг задлан, (шаардлагатай бол) токеноор тоглогчийг тодорхойлно."""

    def decorator(view):
        @csrf_exempt
        @wraps(view)
        def wrapper(request, *args, **kwargs):
            if request.method not in methods:
                return JsonResponse({"ok": False, "error": "method", "message": "Буруу хүсэлт"}, status=405)
            try:
                request.json = json.loads(request.body or b"{}") if request.body else {}
                if not isinstance(request.json, dict):
                    raise ValueError
            except ValueError:
                return JsonResponse({"ok": False, "error": "bad_json", "message": "Буруу хүсэлт"}, status=400)
            request.token = None
            if auth:
                header = request.META.get("HTTP_AUTHORIZATION", "")
                key = header[7:] if header.startswith("Bearer ") else ""
                token = AuthToken.objects.select_related("player").filter(key=key).first() if key else None
                if token is None or token.player.blocked:
                    return JsonResponse({"ok": False, "error": "auth", "message": "Дахин нэвтэрнэ үү"}, status=401)
                if token.revoked:
                    # Өөр төхөөрөмж дээр нэвтэрсэн: нэг удаа мэдэгдээд нэвтрэлтийг устгана
                    token.delete()
                    return JsonResponse({"ok": False, "error": "session_replaced",
                                         "message": "Таны данс өөр төхөөрөмж дээр нэвтэрсэн тул энд гарлаа"}, status=401)
                AuthToken.objects.filter(pk=token.pk).update(last_used=timezone.now())
                request.token = token
            try:
                return view(request, *args, **kwargs)
            except GameError as error:
                return JsonResponse({"ok": False, "error": error.code, "message": error.message}, status=error.status)

        return wrapper

    return decorator


def ok(**data):
    return JsonResponse({"ok": True, **data})


def account(token):
    token.player.refresh_from_db()
    return logic.account_state(token.player, token.device_id)


# ---------------------------------------------------------------- Бүртгэл, нэвтрэлт

def verification_json(verification, instruction):
    """Тоглогчид харуулах заавар: "144773 дугаарт 482916 гэж SMS илгээнэ үү" (sms_uri: утсан дээр мессежийн апп)."""
    return {"verification_id": verification.pk,
            "expires_in": max(0, int((verification.expires - timezone.now()).total_seconds())),
            "sms": instruction}


@api(["POST"], auth=False)
def register_start(request):
    j = request.json
    verification, instruction = logic.start_registration(j.get("name"), j.get("phone"), j.get("password"), j.get("device_id"),
                                                         j.get("platform"), client_ip(request))
    return ok(**verification_json(verification, instruction))


@api(["POST"], auth=False)
def register_confirm(request):
    """Тоглоом мессеж илгээгдэх хүртэл хэдэн секунд тутам асууна: илгээгээгүй бол 409 not_verified."""
    player, token = logic.confirm_registration(request.json.get("verification_id"), client_ip(request))
    return ok(token=token.key, account=logic.account_state(player, token.device_id))


@api(["POST"], auth=False)
def login(request):
    j = request.json
    player, token = logic.login(j.get("phone"), j.get("password"), j.get("device_id"), j.get("platform"), client_ip(request))
    return ok(token=token.key, account=logic.account_state(player, token.device_id))


@api(["POST"])
def logout(request):
    logic.log_event(request.token.player, "logout", {}, request.token.device_id, client_ip(request))
    request.token.delete()
    return ok()


@api(["POST"], auth=False)
def password_reset_start(request):
    verification, instruction = logic.start_password_reset(request.json.get("phone"), request.json.get("device_id", ""),
                                                           client_ip(request))
    return ok(**verification_json(verification, instruction))


@api(["POST"], auth=False)
def password_reset_confirm(request):
    j = request.json
    logic.confirm_password_reset(j.get("verification_id"), j.get("password"), client_ip(request))
    return ok()


# ---------------------------------------------------------------- Данс, аймаг

@api(["GET"])
def account_view(request):
    return ok(account=account(request.token))


@api(["POST"])
def province_claim(request):
    player, source = logic.claim_province(request.token.player, request.token.device_id, request.json.get("province"),
                                          client_ip(request))
    return ok(source=source, account=logic.account_state(player, request.token.device_id))


@api(["POST"])
def province_start(request):
    """Ганцаарчилсан бол {"room": null}; эс бөгөөс нээлттэй өрөө ба relay-ийн тасалбар."""
    player = request.token.player
    room = logic.start_province(player, request.token.device_id, request.json.get("province"), client_ip(request))
    return ok(room=logic.room_json(room, player) if room else None, account=account(request.token))


# ---------------------------------------------------------------- Нээлттэй өрөө

@api(["POST"])
def room_join(request):
    player = request.token.player
    room = logic.join_room(player, request.token.device_id, client_ip(request))
    return ok(room=logic.room_json(room, player), account=account(request.token))


@api(["POST"])
def room_beat(request, room_id):
    room = logic.room_beat(request.token.player, room_id)
    return ok(**logic.room_state(room))


@api(["POST"])
def room_stage(request, room_id):
    logic.save_stage(request.token.player, room_id, request.json.get("stage"), request.json.get("recruits"))
    return ok()


@api(["POST"])
def room_result(request, room_id):
    """{"battle": n, "winner": "host"|"guest"}. Шийдэгдээгүй бол {"settled": false}: хэдэн секунд тутам давтана."""
    player = request.token.player
    settled, winner, room = logic.report_battle(player, request.token.device_id, room_id, request.json.get("battle"),
                                                request.json.get("winner"), client_ip(request))
    data = {"settled": settled, "winner": winner, "account": account(request.token)}
    if settled and room.status == room.OPEN and room.host_id == player.pk:
        data["room"] = logic.room_json(room, player)  # (зочин ялсан бол: шинэ хостын тасалбар, ахиц, цэргүүд)
    return ok(**data)


@api(["POST"])
def room_leave(request, room_id):
    logic.leave_room(request.token.player, request.token.device_id, room_id, client_ip(request))
    return ok(account=account(request.token))


@api(["POST"])
def room_complete(request, room_id):
    logic.complete_room(request.token.player, request.token.device_id, room_id, client_ip(request))
    return ok(account=account(request.token))


# ---------------------------------------------------------------- Дэлгүүр (Gold)

@api(["POST"])
def shop_buy(request):
    """{"item": "damage"|"fire_rate"|"magazine"|"health"|"soldier", "level": <тоглоомын харж буй одоогийн түвшин>}"""
    player = logic.shop_buy(request.token.player, request.token.device_id, request.json.get("item"),
                            request.json.get("level"), client_ip(request))
    return ok(account=logic.account_state(player, request.token.device_id))


# ---------------------------------------------------------------- Төлбөр

def invoice_json(invoice, info=None):
    data = {"id": invoice.pk, "kind": invoice.kind, "amount": invoice.amount, "credits": invoice.credits,
            "gold": invoice.gold, "status": invoice.status, "applied": invoice.applied}
    if info:
        data.update(qr_text=info["qr_text"], qr_image=info["qr_image"], short_url=info["short_url"], urls=info["urls"])
    return data


@api(["POST"])
def invoice_create(request):
    invoice, info = logic.create_invoice(request.token.player, request.token.device_id, request.json.get("kind"),
                                         request.json.get("platform"), client_ip(request), request.json.get("pack"))
    return ok(invoice=invoice_json(invoice, info))


# ---------------------------------------------------------------- Вэбээр Gold цэнэглэх

@api(["POST"], auth=False)
def web_login(request):
    player, token = logic.web_login(request.json.get("phone"), request.json.get("password"), client_ip(request))
    return ok(token=token.key, name=player.name, gold=player.gold, packs=logic.gold_packs())


@api(["GET"], auth=False)
def gold_packs(request):
    return ok(packs=logic.gold_packs())


def topup_page(request):
    """Gold цэнэглэх хуудас (game/templates/topup.html): нэвтрээд багц сонгож QPay-ээр төлнө."""
    from django.shortcuts import render

    return render(request, "topup.html")


@api(["GET"])
def invoice_status(request, invoice_id):
    invoice = request.token.player.invoices.filter(pk=invoice_id).first()
    if invoice is None:
        raise GameError("not_found", "Нэхэмжлэх олдсонгүй", 404)
    invoice = logic.refresh_invoice(invoice, "poll")
    return ok(invoice=invoice_json(invoice), account=account(request.token))


@csrf_exempt
def qpay_callback(request, invoice_id, key):
    """QPay төлбөр орсныг мэдэгдэнэ. Мэдэгдэлд итгэхгүй: QPay-ээс дахин шалгаж байж эрх олгоно."""
    invoice = Invoice.objects.filter(pk=invoice_id, callback_key=key).first()
    if invoice is not None:
        logic.refresh_invoice(invoice, "callback")
    return HttpResponse("SUCCESS")


# ---------------------------------------------------------------- Лог, хадгалалт

@api(["POST"])
def events(request):
    """Тоглоомын үйл явдлууд (даалгавар биелсэн, аймаг чөлөөлсөн г.м.): {"events": [{"kind": ..., "data": {...}}]}"""
    items = request.json.get("events")
    if not isinstance(items, list) or len(items) > MAX_EVENTS:
        raise GameError("bad_events", "Буруу хүсэлт")
    stored = 0
    for item in items:
        kind = item.get("kind") if isinstance(item, dict) else None
        data = item.get("data", {}) if isinstance(item, dict) else None
        if not isinstance(kind, str) or not re.fullmatch(r"[a-z_]{2,32}", kind) or not isinstance(data, dict):
            continue
        if len(json.dumps(data)) > 2000:
            continue
        logic.log_event(request.token.player, "game_" + kind, data, request.token.device_id, client_ip(request))
        stored += 1
    return ok(stored=stored)


@api(["GET", "PUT"])
def progress(request):
    """Тоглоомын хадгалалтыг (savegame.txt-ийн агуулга) серверт хадгална, өөр төхөөрөмжөөс татна."""
    player = request.token.player
    if request.method == "PUT":
        text = request.json.get("progress")
        if not isinstance(text, str) or len(text) > MAX_PROGRESS:
            raise GameError("bad_progress", "Хадгалалт буруу байна")
        player.progress = text
        player.progress_updated = timezone.now()
        player.save(update_fields=["progress", "progress_updated"])
        return ok()
    return ok(progress=player.progress,
              updated=player.progress_updated.isoformat() if player.progress_updated else None)
