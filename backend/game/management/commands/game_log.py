"""Тоглолтын лог (game/gamelog.py): өрөө эсвэл тоглогчоор хост, зочин, сервер гурвын мөрийг цагийн дарааллаар.

    python3 manage.py game_log --room 146
    python3 manage.py game_log --player namjilmaa --hours 3
    python3 manage.py game_log --room 146 --steps      # зөвхөн давталтын алхмууд
"""
from django.core.management.base import BaseCommand, CommandError

from game import gamelog


class Command(BaseCommand):
    help = "Тоглолтын лог: өрөө эсвэл тоглогчоор, хост, зочин, серверийн мөрүүд нэг цагийн дарааллаар"

    def add_arguments(self, parser):
        parser.add_argument("--room", type=int, help="өрөөний дугаар")
        parser.add_argument("--player", help="тоглогчийн нэр, утасны сүүлийн оронгууд эсвэл id")
        parser.add_argument("--hours", type=float, default=6.0, help="сүүлийн хэдэн цаг (анхдагч 6)")
        parser.add_argument("--steps", action="store_true", help="зөвхөн давталтын алхмууд")

    def handle(self, *args, **options):
        player = None
        if options["player"]:
            player = gamelog.find_player(options["player"])
            if not player:
                raise CommandError(f"Тоглогч олдсонгүй: {options['player']}")
        rows = gamelog.query(room=options["room"], player=player, hours=options["hours"], steps_only=options["steps"])
        if not rows:
            self.stdout.write("Лог олдсонгүй")
            return
        self.stdout.write(gamelog.format_lines(rows))
