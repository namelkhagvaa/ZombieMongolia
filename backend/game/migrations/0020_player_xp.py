from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('game', '0019_remove_ip_limits'),
    ]

    operations = [
        migrations.AddField(
            model_name='player',
            name='xp',
            field=models.PositiveIntegerField(default=0, verbose_name='Туршлага (XP): үе давах, тулаанд ялах'),
        ),
    ]
