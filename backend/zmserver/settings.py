"""Zombie Mongolia — тоглогчийн бүртгэл, төлбөр, лог (Django).

Бүх нууц мэдээлэл ба орчны тохиргоо backend/.env файлаас (эсвэл орчны хувьсагчаас) уншигдана; жишээг
.env.example-аас үз. Хөгжүүлэлтэд SQLite, серверт MySQL (DB_ENGINE=mysql).
"""
import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent


def _load_env(path):
    """KEY=VALUE мөрүүдийг орчны хувьсагчид оруулна (аль хэдийн тохируулсныг дарахгүй)."""
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


_load_env(BASE_DIR / ".env")


def env(key, default=""):
    return os.environ.get(key, default)


def env_bool(key, default=False):
    return env(key, "1" if default else "0").lower() in ("1", "true", "yes", "on")


DEBUG = env_bool("DEBUG", False)
SECRET_KEY = env("SECRET_KEY") or ("dev-only-insecure-key" if DEBUG else "")
if not SECRET_KEY:
    raise RuntimeError("SECRET_KEY тохируулаагүй байна (backend/.env)")

ALLOWED_HOSTS = [h.strip() for h in env("ALLOWED_HOSTS", "localhost,127.0.0.1").split(",") if h.strip()]
CSRF_TRUSTED_ORIGINS = [o.strip() for o in env("CSRF_TRUSTED_ORIGINS", "").split(",") if o.strip()]
# Хөтөч дээрх тоглоом өөр хаягаас (жишээ нь хөгжүүлэлтийн http://localhost:8080) API руу хандах эрх
CORS_ALLOWED_ORIGINS = [o.strip() for o in env("CORS_ALLOWED_ORIGINS", "").split(",") if o.strip()]

INSTALLED_APPS = [
    "django.contrib.admin",
    "django.contrib.auth",
    "django.contrib.contenttypes",
    "django.contrib.sessions",
    "django.contrib.messages",
    "django.contrib.staticfiles",
    "game",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    "game.middleware.SimpleCorsMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "django.middleware.common.CommonMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "django.contrib.messages.middleware.MessageMiddleware",
    "django.middleware.clickjacking.XFrameOptionsMiddleware",
]

ROOT_URLCONF = "zmserver.urls"

TEMPLATES = [
    {
        "BACKEND": "django.template.backends.django.DjangoTemplates",
        "DIRS": [],
        "APP_DIRS": True,
        "OPTIONS": {
            "context_processors": [
                "django.template.context_processors.request",
                "django.contrib.auth.context_processors.auth",
                "django.contrib.messages.context_processors.messages",
            ],
        },
    },
]

WSGI_APPLICATION = "zmserver.wsgi.application"

# Өгөгдлийн сан: DB_ENGINE=sqlite (анхдагч) эсвэл mysql. MySQL-ийг utf8mb4-ээр үүсгэнэ (README).
if env("DB_ENGINE", "sqlite") == "mysql":
    DATABASES = {
        "default": {
            "ENGINE": "django.db.backends.mysql",
            "NAME": env("DB_NAME", "zombie"),
            "USER": env("DB_USER", "zombie"),
            "PASSWORD": env("DB_PASSWORD"),
            "HOST": env("DB_HOST", "127.0.0.1"),
            "PORT": env("DB_PORT", "3306"),
            "OPTIONS": {"charset": "utf8mb4", "init_command": "SET sql_mode='STRICT_TRANS_TABLES'"},
        }
    }
else:
    DATABASES = {
        "default": {
            "ENGINE": "django.db.backends.sqlite3",
            "NAME": BASE_DIR / env("SQLITE_PATH", "db.sqlite3"),
        }
    }

AUTH_PASSWORD_VALIDATORS = []

LANGUAGE_CODE = "mn"
TIME_ZONE = "Asia/Ulaanbaatar"
USE_I18N = True
USE_TZ = True

STATIC_URL = env("STATIC_URL", "/api/static/")
STATIC_ROOT = BASE_DIR / "static"
DEFAULT_AUTO_FIELD = "django.db.models.BigAutoField"

# Серверт nginx-ийн ард: https-ийг nginx дамжуулна
if not DEBUG:
    SECURE_PROXY_SSL_HEADER = ("HTTP_X_FORWARDED_PROTO", "https")
    SESSION_COOKIE_SECURE = True
    CSRF_COOKIE_SECURE = True

# --- QPay (game/qpay.py) ---
# QPAY_MODE: mock (хөгжүүлэлт: бодит төлбөргүй), sandbox, production
QPAY_MODE = env("QPAY_MODE", "mock")
QPAY_BASE_URL = env("QPAY_BASE_URL", "https://merchant-sandbox.qpay.mn")
QPAY_USERNAME = env("QPAY_USERNAME")
QPAY_PASSWORD = env("QPAY_PASSWORD")
QPAY_INVOICE_CODE = env("QPAY_INVOICE_CODE")
# QPay төлбөр орсныг мэдэгдэх хаягийн эхлэл: <PUBLIC_BASE_URL>/api/qpay/callback/...
PUBLIC_BASE_URL = env("PUBLIC_BASE_URL", "http://localhost:8010")

# --- Утас баталгаажуулалт (game/sms.py) ---
# SMS_MODE: console (хөгжүүлэлт: кодыг логт бичнэ), verifymn
SMS_MODE = env("SMS_MODE", "console")

# Нээлттэй өрөөний тасалбар: relay сервер (server/relay.js) ижил нууц үгээр гарын үсгийг шалгана
RELAY_SECRET = env("RELAY_SECRET") or ("dev-only-relay-secret" if DEBUG else "")
# Тоглогчийн бодит IP-г агуулах толгой (proxy/CDN-ийн ард): жишээ нь Cloudflare-ийн ард "HTTP_CF_CONNECTING_IP".
# Хоосон бол X-Forwarded-For (nginx), эс бөгөөс REMOTE_ADDR. Зөвхөн сервер тэр proxy-ээр л хүрэх боломжтой үед тохируулна
# (эс бөгөөс толгойг хуурамчаар тавьж IP-ийн хязгаарыг тойрно).
CLIENT_IP_HEADER = env("CLIENT_IP_HEADER", "")
VERIFYMN_API_URL = env("VERIFYMN_API_URL")
VERIFYMN_API_KEY = env("VERIFYMN_API_KEY")


# Бодит ажиллагаа (DEBUG=0): туршилтын төлбөр, SMS горимыг хориглоно
if not DEBUG and (QPAY_MODE == "mock" or SMS_MODE == "console"):
    raise RuntimeError("Серверт QPAY_MODE=production, SMS_MODE=verifymn байх ёстой (backend/.env)")

LOGGING = {
    "version": 1,
    "disable_existing_loggers": False,
    "handlers": {"console": {"class": "logging.StreamHandler"}},
    "loggers": {"game": {"handlers": ["console"], "level": "INFO"}},
}
