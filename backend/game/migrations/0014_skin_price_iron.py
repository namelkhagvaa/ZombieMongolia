from django.db import migrations, models


def copy_price(apps, schema_editor):
    # Өмнө нь хоёр дүр нэг үнэтэй байсан: IRON-д одоогийн үнийг хуулна
    GameSettings = apps.get_model("game", "GameSettings")
    for settings in GameSettings.objects.all():
        settings.skin_price_iron = settings.skin_price
        settings.save(update_fields=["skin_price_iron"])


class Migration(migrations.Migration):
    dependencies = [("game", "0013_coop_reason")]

    operations = [
        migrations.AlterField(
            model_name="gamesettings",
            name="skin_price",
            field=models.PositiveIntegerField(default=50, verbose_name="DEMON дүрээр тоглох (Gold)"),
        ),
        migrations.AddField(
            model_name="gamesettings",
            name="skin_price_iron",
            field=models.PositiveIntegerField(default=50, verbose_name="IRON дүрээр тоглох (Gold)"),
        ),
        migrations.RunPython(copy_price, migrations.RunPython.noop),
    ]
