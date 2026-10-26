import django
from django.apps import AppConfig
from django.db.backends.signals import connection_created


def _sqlite_wal(sender, connection, **kwargs):
    """Хуучин Django (5.1-ээс өмнө: init_command байхгүй): SQLite холболт бүрд WAL, бичих түгжээг хүлээх хугацаа."""
    if connection.vendor == "sqlite":
        with connection.cursor() as cursor:
            cursor.execute("PRAGMA journal_mode=WAL;")
            cursor.execute("PRAGMA synchronous=NORMAL;")
            cursor.execute("PRAGMA busy_timeout=20000;")


class GameConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "game"
    verbose_name = "Тоглоом"

    def ready(self):
        if django.VERSION < (5, 1):
            connection_created.connect(_sqlite_wal)
