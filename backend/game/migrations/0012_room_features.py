from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("game", "0011_coop_settings"),
    ]

    operations = [
        migrations.AddField(
            model_name="room",
            name="code",
            field=models.CharField(blank=True, db_index=True, max_length=4, verbose_name="Найзын код"),
        ),
        migrations.AddField(
            model_name="room",
            name="pvp",
            field=models.BooleanField(default=True, verbose_name="Нээлттэй (тулаан)"),
        ),
        migrations.AddField(
            model_name="room",
            name="guest_coop",
            field=models.BooleanField(default=False, verbose_name="Зочин кодоор орсон найз"),
        ),
    ]
