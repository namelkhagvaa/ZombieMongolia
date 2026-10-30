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

from . import gamelog, logic
from .logic import GameError
from .models import AuthToken, Invoice

MAX_PROGRESS = 64 * 1024
MAX_EVENTS = 50


def client_ip(request):
    return logic.request_ip(request)


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
def register(request):
    """{name, phone, password, device_id, platform} -> token, account: SMS баталгаажуулалтгүй шууд данс."""
    j = request.json
    player, token = logic.register(j.get("name"), j.get("phone"), j.get("password"), j.get("device_id"), j.get("platform"),
                                   client_ip(request))
    return ok(token=token.key, account=logic.account_state(player, token.device_id))


@api(["POST"], auth=False)
def register_start(request):
    """(Хуучин хувилбарын тоглоомд: SMS-ээр баталгаажуулж бүртгэх.)"""
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
def phone_verify_start(request):
    """Нэвтэрсэн тоглогч утсаа баталгаажуулна: 144773 руу мессеж илгээх заавар (бүртгэлийнхтэй ижил)."""
    verification, instruction = logic.start_phone_verify(request.token.player, request.token.device_id, client_ip(request))
    return ok(**verification_json(verification, instruction))


@api(["POST"])
def phone_verify_confirm(request):
    """{verification_id}: мессеж ирээгүй бол 409 not_verified (тоглоом давтан асууна)."""
    player = logic.confirm_phone_verify(request.token.player, request.json.get("verification_id"), client_ip(request))
    return ok(account=logic.account_state(player, request.token.device_id))


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
    logic.maybe_expire_rooms()  # (тоглогч бүр минут тутам: орхигдсон өрөөний түгжигдсэн барьцаа суллагдана)
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
    try:
        skin = int(request.json.get("skin") or 0)
    except (TypeError, ValueError):
        skin = 0
    room = logic.start_province(player, request.token.device_id, request.json.get("province"), client_ip(request), skin,
                                request.json.get("protocol", 0))
    return ok(room=logic.room_json(room, player) if room else None, account=account(request.token))


# ---------------------------------------------------------------- Нээлттэй өрөө

@api(["POST"])
def room_join(request):
    player = request.token.player
    room_id = request.json.get("room")
    room_id = int(room_id) if isinstance(room_id, int) or (isinstance(room_id, str) and room_id.isdigit()) else None
    try:
        room = logic.join_room(player, request.token.device_id, client_ip(request), request.json.get("protocol", 0), room_id)
    except GameError as error:
        # Өрөөнд оруулаагүй шалтгаан (зэрэг, хувилбар, Gold, маргаан, өрөө алга): админы тоглогчийн үйл явдалд харагдана
        logic.log_event(player, "room_join_refused", {"reason": error.code, "message": error.message[:200], "room": room_id,
                                                      "protocol": request.json.get("protocol", 0)},
                        request.token.device_id, client_ip(request))
        raise
    return ok(room=logic.room_json(room, player), account=account(request.token))


@api(["GET"])
def room_list(request):
    """?protocol=16: тулааны нээлттэй өрөөнүүд (жагсаалтаас сонгож орно)."""
    protocol = request.GET.get("protocol", "0")
    return ok(**logic.list_rooms(request.token.player, int(protocol) if protocol.isdigit() else 0))


@api(["POST"])
def room_coop(request):
    """{"code": "1234"}: найзын өрөөнд хамт тоглоно (тулаангүй)."""
    player = request.token.player
    room = logic.join_coop(player, request.token.device_id, request.json.get("code"), client_ip(request),
                           request.json.get("protocol", 0))
    return ok(room=logic.room_json(room, player), account=account(request.token))


@api(["POST"])
def room_accept(request, room_id):
    """Хост: зочин тоглоомоор холбогдлоо (тулаан тоологдож эхэлнэ)."""
    room = logic.accept_guest(request.token.player, room_id)
    return ok(**logic.room_state(room))


@api(["POST"])
def room_beat(request, room_id):
    logic.maybe_expire_rooms()
    room = logic.room_beat(request.token.player, room_id)
    return ok(**logic.room_state(room))


@api(["POST"])
def room_guest_lost(request, room_id):
    """Хост: {"battle": n, "lost": <энэ тулаанд үхсэн зочны Gold-ийн цэрэг, нийт>}"""
    logic.guest_soldiers_lost(request.token.player, room_id, request.json.get("battle"), request.json.get("lost"))
    return ok()


@api(["POST"])
def room_stage(request, room_id):
    logic.save_stage(request.token.player, room_id, request.json.get("stage"), request.json.get("recruits"))
    return ok()


@api(["POST"])
def room_result(request, room_id):
    """{"battle": n, "winner": "host"|"guest"|"none"}. Шийдэгдээгүй бол {"settled": false}: хэдэн секунд тутам давтана."""
    player = request.token.player
    settled, winner, room = logic.report_battle(player, request.token.device_id, room_id, request.json.get("battle"),
                                                request.json.get("winner"), client_ip(request),
                                                absent=request.json.get("absent") is True)
    data = {"settled": settled, "winner": winner, "account": account(request.token)}
    if settled:
        # Энэ тоглогчийн хувьд: хүчингүй (зөрсөн), ялсан (өрөөний хост: хэвээр эсвэл зочноос шинэ), ялагдсан
        own = room.status == room.OPEN and room.host_id == player.pk
        data["result"] = "void" if winner == "void" else "won" if own else "lost"
        info = getattr(room, "settle_info", None)
        if info and info["winner"] == player.pk and info["missed"]:
            data["missed"] = info["stake"]  # утсаа баталгаажуулаагүй тул хожил олгоогүй
        if own:
            data["room"] = logic.room_json(room, player)  # (зочин ялсан бол: шинэ хостын тасалбар, ахиц, цэргүүд)
    return ok(**data)


@api(["POST"])
def room_leave(request, room_id):
    logic.leave_room(request.token.player, request.token.device_id, room_id, client_ip(request))
    return ok(account=account(request.token))


@api(["POST"])
def room_complete(request, room_id):
    missed = logic.complete_room(request.token.player, request.token.device_id, room_id, client_ip(request))
    return ok(account=account(request.token), missed=missed)


# ---------------------------------------------------------------- Дэлгүүр (Gold)

@api(["POST"])
def shop_buy(request):
    """{"item": "damage"|"fire_rate"|"magazine"|"health"|"soldier", "level": <тоглоомын харж буй одоогийн түвшин>}"""
    player = logic.shop_buy(request.token.player, request.token.device_id, request.json.get("item"),
                            request.json.get("level"), client_ip(request))
    return ok(account=logic.account_state(player, request.token.device_id))


@api(["GET", "POST"])
def cashouts(request):
    """GET: сүүлийн хүсэлтүүд. POST {"gold", "bank", "account", "holder"}: татах хүсэлт (Gold шууд хасагдана), дараа нь
    тоглогч утсаа баталгаажуулна (sms заавар)."""
    player = request.token.player
    if request.method == "GET":
        logic.expire_cashouts(player)
        return ok(cashouts=[logic.cashout_json(c) for c in player.cashouts.all()[:20]], account=account(request.token))
    j = request.json
    cashout, verification, instruction = logic.start_cashout(player, request.token.device_id, j.get("gold"), j.get("bank"),
                                                             j.get("account"), j.get("holder"), client_ip(request))
    return ok(cashout=logic.cashout_json(cashout), **verification_json(verification, instruction), account=account(request.token))


@api(["GET"])
def cashout_status(request, cashout_id):
    """Тоглоом SMS илгээгдтэл хэдэн секунд тутам асууна: verifying -> pending (баталгаажсан) эсвэл expired."""
    cashout = logic.refresh_cashout(logic.player_cashout(request.token.player, cashout_id))
    return ok(cashout=logic.cashout_json(cashout), account=account(request.token))


@api(["POST"])
def cashout_cancel(request, cashout_id):
    cashout = logic.cancel_cashout(request.token.player, cashout_id)
    return ok(cashout=logic.cashout_json(cashout), account=account(request.token))


@api(["POST"])
def soldiers_lost(request):
    """{"soldiers": <үлдсэн тоо>}: тоглолтод үхсэн Gold-ийн цэргүүдийг хасна"""
    player = logic.lose_soldiers(request.token.player, request.token.device_id, request.json.get("soldiers"),
                                 client_ip(request))
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


@api(["POST"])
def logs(request):
    """Тоглолтын техникийн лог (game/gamelog.py): {"now", "platform", "build", "session", "entries": [...]}"""
    try:
        stored = gamelog.store(request.token.player, request.json)
    except gamelog.LogError as error:
        raise GameError("too_many_logs" if error.status == 429 else "bad_logs", str(error), error.status)
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
