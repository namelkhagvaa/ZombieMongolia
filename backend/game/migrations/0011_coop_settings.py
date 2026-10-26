from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("game", "0010_bonus_gold"),
    ]

    operations = [
        migrations.AddField(
            model_name="gamesettings",
            name="coop_reward",
            field=models.PositiveIntegerField(default=30, verbose_name="Хамт давсны шагнал, хүн бүрд (Gold)"),
        ),
        migrations.AddField(
            model_name="gamesettings",
            name="coop_daily_limit",
            field=models.PositiveSmallIntegerField(default=5, verbose_name="Хамт давсны шагнал өдөрт (удаа)"),
        ),
    ]
