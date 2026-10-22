from django.contrib import admin
from django.urls import include, path

admin.site.site_header = "Zombie Mongolia — удирдлага"
admin.site.site_title = "Zombie Mongolia"
admin.site.index_title = "Тоглогч, төлбөр, лог"

urlpatterns = [
    path("api/admin/", admin.site.urls),
    path("api/", include("game.urls")),
]
