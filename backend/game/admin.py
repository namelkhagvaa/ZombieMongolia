from django.contrib import admin, messages

from . import logic
from .models import AuthToken, Device, Event, GameSettings, Invoice, Player, UnlockedProvince, Verification


@admin.register(GameSettings)
class GameSettingsAdmin(admin.ModelAdmin):
    list_display = ["__str__", "free_provinces", "pack_size", "pack_price", "device_change_price", "max_devices"]

    def has_add_permission(self, request):
        return not GameSettings.objects.exists()

    def has_delete_permission(self, request, obj=None):
        return False


class UnlockedInline(admin.TabularInline):
    model = UnlockedProvince
    extra = 0
    readonly_fields = ["created"]


class DeviceInline(admin.TabularInline):
    model = Device
    extra = 0
    readonly_fields = ["device_id", "platform", "bound_at"]


class InvoiceInline(admin.TabularInline):
    model = Invoice
    extra = 0
    fields = ["kind", "amount", "status", "applied", "created", "paid_at"]
    readonly_fields = fields
    can_delete = False
    show_change_link = True


@admin.register(Player)
class PlayerAdmin(admin.ModelAdmin):
    list_display = ["name", "phone", "free_used", "credits", "province_count", "blocked", "created"]
    list_filter = ["blocked", "created"]
    search_fields = ["name", "phone"]
    readonly_fields = ["password", "created", "progress_updated"]
    inlines = [UnlockedInline, DeviceInline, InvoiceInline]

    @admin.display(description="Нээсэн аймаг")
    def province_count(self, obj):
        return obj.provinces.count()


@admin.register(Device)
class DeviceAdmin(admin.ModelAdmin):
    list_display = ["device_id", "player", "platform", "active", "bound_at"]
    list_filter = ["active", "platform"]
    search_fields = ["device_id", "player__phone", "player__name"]
    actions = ["release"]

    @admin.action(description="Суллах: төхөөрөмжийг устгана (шинэ данс нээх боломжтой болно)")
    def release(self, request, queryset):
        for device in queryset:
            logic.log_event(device.player, "admin_device_released", {"device": device.device_id, "admin": request.user.username})
        count = queryset.count()
        queryset.delete()
        self.message_user(request, f"{count} төхөөрөмж суллагдлаа")


@admin.register(Invoice)
class InvoiceAdmin(admin.ModelAdmin):
    list_display = ["id", "player", "kind", "amount", "status", "applied", "created", "paid_at"]
    list_filter = ["kind", "status", "applied", "created"]
    search_fields = ["player__phone", "player__name", "qpay_invoice_id", "qpay_payment_id"]
    readonly_fields = ["player", "kind", "amount", "credits", "device_id", "platform", "qpay_invoice_id", "qpay_payment_id",
                       "callback_key", "created", "paid_at", "applied"]
    actions = ["check_qpay", "mark_paid"]

    @admin.action(description="QPay-ээс төлөгдсөн эсэхийг шалгах")
    def check_qpay(self, request, queryset):
        for invoice in queryset:
            logic.refresh_invoice(invoice, "admin_check")
        self.message_user(request, "Шалгалаа")

    @admin.action(description="Төлсөн гэж гараар тэмдэглэж эрх олгох (QPay-ээс баталгаажуулсан бол л)")
    def mark_paid(self, request, queryset):
        for invoice in queryset:
            logic.apply_payment(invoice.pk, f"manual:{request.user.username}", "admin")
        self.message_user(request, "Эрх олгогдлоо", messages.WARNING)


@admin.register(Event)
class EventAdmin(admin.ModelAdmin):
    list_display = ["created", "player", "kind", "data", "device_id", "ip"]
    list_filter = ["kind", "created"]
    search_fields = ["player__phone", "player__name", "kind", "device_id"]
    date_hierarchy = "created"

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False  # лог засагдахгүй


@admin.register(Verification)
class VerificationAdmin(admin.ModelAdmin):
    list_display = ["phone", "purpose", "attempts", "used", "created", "expires"]
    list_filter = ["purpose", "used"]
    search_fields = ["phone"]
    exclude = ["code_hash", "payload"]


@admin.register(AuthToken)
class AuthTokenAdmin(admin.ModelAdmin):
    list_display = ["player", "device_id", "created", "last_used", "revoked"]
    search_fields = ["player__phone", "device_id"]
    exclude = ["key"]
