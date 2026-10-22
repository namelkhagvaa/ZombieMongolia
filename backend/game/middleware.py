from django.conf import settings
from django.http import HttpResponse


class SimpleCorsMiddleware:
    """CORS_ALLOWED_ORIGINS-д бичсэн хаягаас (хөтөч дээрх тоглоом) /api/ руу хандахыг зөвшөөрнө.
    Серверт тоглоом API-тай нэг домэйнд байрлавал хоосон үлдээнэ."""

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        origin = request.META.get("HTTP_ORIGIN", "")
        allowed = origin and origin in settings.CORS_ALLOWED_ORIGINS and request.path.startswith("/api/")
        if allowed and request.method == "OPTIONS":
            response = HttpResponse(status=204)
        else:
            response = self.get_response(request)
        if allowed:
            response["Access-Control-Allow-Origin"] = origin
            response["Access-Control-Allow-Headers"] = "Authorization, Content-Type"
            response["Access-Control-Allow-Methods"] = "GET, POST, PUT, PATCH, OPTIONS"
            response["Vary"] = "Origin"
        return response
