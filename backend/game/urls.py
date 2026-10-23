from django.urls import path

from . import manage_api as manage
from . import views

urlpatterns = [
    path("register/start/", views.register_start),
    path("register/confirm/", views.register_confirm),
    path("login/", views.login),
    path("logout/", views.logout),
    path("password/reset/start/", views.password_reset_start),
    path("password/reset/confirm/", views.password_reset_confirm),
    path("account/", views.account_view),
    path("provinces/claim/", views.province_claim),
    path("provinces/start/", views.province_start),
    path("shop/buy/", views.shop_buy),
    path("rooms/join/", views.room_join),
    path("rooms/<int:room_id>/beat/", views.room_beat),
    path("rooms/<int:room_id>/leave/", views.room_leave),
    path("rooms/<int:room_id>/complete/", views.room_complete),
    path("invoices/", views.invoice_create),
    path("invoices/<int:invoice_id>/", views.invoice_status),
    path("qpay/callback/<int:invoice_id>/<str:key>/", views.qpay_callback),
    path("events/", views.events),
    path("progress/", views.progress),
    # Админ самбар (admin/: React)
    path("manage/login/", manage.login),
    path("manage/me/", manage.me),
    path("manage/dashboard/", manage.dashboard),
    path("manage/players/", manage.players),
    path("manage/players/<int:player_id>/", manage.player_detail),
    path("manage/players/<int:player_id>/province/", manage.player_province),
    path("manage/players/<int:player_id>/devices/<int:device_pk>/release/", manage.player_device_release),
    path("manage/players/<int:player_id>/logout/", manage.player_logout),
    path("manage/players/<int:player_id>/password/", manage.player_password),
    path("manage/invoices/", manage.invoices),
    path("manage/invoices/<int:invoice_id>/check/", manage.invoice_check),
    path("manage/invoices/<int:invoice_id>/mark-paid/", manage.invoice_mark_paid),
    path("manage/events/", manage.events),
    path("manage/events/kinds/", manage.event_kinds),
    path("manage/settings/", manage.settings_view),
]
