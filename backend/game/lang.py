"""Тоглогчид харагдах серверийн мессежийн англи хувилбар. Тоглоом англи горимд хүсэлтэд ?lang=en нэмнэ (src/Online.cpp);
views.api хариуны message, pvp_note, blocked, reason талбаруудыг энд орчуулна. Орчуулгагүй бол монголоор нь үлдэнэ.
Шинэ мессеж нэмбэл энд англи хувилбарыг нь нэмнэ (тоотой бол PATTERNS)."""
import re

FIELDS = ("message", "pvp_note", "blocked", "reason")

EN = {
    "Буруу хүсэлт": "Bad request",
    "Дахин нэвтэрнэ үү": "Please sign in again",
    "Таны данс өөр төхөөрөмж дээр нэвтэрсэн тул энд гарлаа": "Your account signed in on another device, so you were signed out here",
    "Сүүлийн 7 хоногт олон тулааны үр дүн зөрсөн тул тулаанд түр орох боломжгүй":
        "Too many battle results didn't match in the last 7 days, so you can't battle for now",
    "Тулаанд орохын тулд тоглоомоо шинэ хувилбараар шинэчилнэ үү": "Update the game to the latest version to battle",
    "Утасны дугаар 8 оронтой байх ёстой": "The phone number must have 8 digits",
    "Төхөөрөмжийн мэдээлэл буруу байна": "Invalid device information",
    "Хэт олон удаа оролдсон байна. Хэсэг хүлээгээд дахин оролдоно уу": "Too many attempts. Wait a little and try again",
    "Хугацаа дууссан. Дахин эхлүүлнэ үү": "Time is up. Please start again",
    "Мессеж хүлээж байна": "Waiting for the text",
    "Нэр 2-60 тэмдэгт байна": "The name must be 2-60 characters",
    "Энэ дугаар бүртгэлтэй байна. Нэвтэрнэ үү": "This number is already registered. Please sign in",
    "Энэ дугаар бүртгэлтэй байна. Нэвтэрнэ үү (эсвэл \"Нууц үг мартсан\")":
        "This number is already registered. Please sign in (or use \"Forgot password\")",
    "Энэ төхөөрөмж өөр дансанд бүртгэлтэй байна": "This device is registered to another account",
    "Хэт олон удаа буруу оруулсан. 15 минутын дараа дахин оролдоно уу": "Too many wrong attempts. Try again in 15 minutes",
    "Энэ дугаар бүртгэлгүй байна. Бүртгүүлнэ үү": "This number is not registered. Please register",
    "Энэ дугаар бүртгэлгүй байна": "This number is not registered",
    "Утас эсвэл нууц үг буруу байна": "Wrong phone number or password",
    "Энэ данс хаагдсан байна": "This account has been blocked",
    "Дансанд утасны дугаар алга": "The account has no phone number",
    "Утас аль хэдийн баталгаажсан": "The phone is already verified",
    "Аймаг буруу байна": "Invalid province",
    "Энэ төхөөрөмж дээр тоглохын тулд төхөөрөмж солих төлбөр төлнө": "To play on this device, pay the device change fee",
    "Өрөө хаагдсан байна": "The room is closed",
    "Хэт олон удаа буруу код оруулсан. 10 минутын дараа оролдоно уу": "Too many wrong codes. Try again in 10 minutes",
    "Код 6 оронтой байна": "The code has 6 digits",
    "Gold хүрэлцэхгүй байна": "Not enough Gold",
    "Gold хүрэлцэхгүй байна (нэг хэсэг нь өрөөнд барьцаалагдсан)": "Not enough Gold (some of it is staked in a room)",
    "Утасны апп-аас Gold татах боломжгүй": "Gold can't be withdrawn from the phone app",
    "Gold татах хаалттай байна": "Gold withdrawal is closed",
    "Gold татахын тулд эхлээд утсаа баталгаажуулна уу": "Verify your phone first to withdraw Gold",
    "Татах Gold-оо оруулна уу": "Enter the Gold to withdraw",
    "Банкаа сонгоно уу": "Choose your bank",
    "Дансны дугаар 6-20 оронтой тоо байна": "The account number must be 6-20 digits",
    "Данс эзэмшигчийн нэрийг оруулна уу": "Enter the account holder's name",
    "Өмнөх хүсэлтээ баталгаажуулна уу эсвэл цуцална уу": "Confirm or cancel your previous request first",
    "Хүсэлт олдсонгүй": "Request not found",
    "Баталгаажсан хүсэлтийг цуцлах боломжгүй": "A confirmed request can't be cancelled",
    "Зөвхөн баталгаажсан (шилжүүлэг хүлээж буй) хүсэлтийг олгоно": "Only confirmed requests (awaiting transfer) can be paid",
    "Хүсэлт аль хэдийн шийдэгдсэн": "The request has already been handled",
    "Үүнийг тоглоом дотроос хийнэ": "Do this in the game",
    "Бүх аймаг үнэгүй болсон": "All provinces are free now",
    "Төхөөрөмжийг Gold-оор солино. Тоглоомоо шинэчилнэ үү": "Devices are changed with Gold. Please update the game",
    "Gold-ийг зөвхөн вэб хувилбараас худалдаж авна": "Gold can only be bought in the web version",
    "Утас баталгаажуулах үйлчилгээ түр ажиллахгүй байна": "Phone verification is temporarily unavailable",
    "Энэ дугаар баталгаажаагүй дансанд бүртгэлтэй. Дугаар таных бол мессежээр баталгаажуулж бүртгүүлнэ үү":
        "This number belongs to an unverified account. If the number is yours, verify it by text and register",
    "Сүүлийн долоо хоногт олон тулааны үр дүн зөрсөн тул тулаант өрөөнд түр орох боломжгүй":
        "Too many battle results didn't match in the last week, so you can't join battle rooms for now",
    "Сүүлийн 10 минутад олон удаа тулаанд ороод холбогдож чадсангүй. Сүлжээ, тоглоомын хувилбараа шалгаад хэдэн минутын дараа оролдоно уу":
        "You failed to connect to battles several times in the last 10 minutes. Check your network and game version and try again in a few minutes",
    "Одоогоор нээлттэй өрөө алга. Дараа дахин оролдоно уу": "No open rooms right now. Please try again later",
    "Ийм кодтой өрөө алга (найз тань аймгаа эхлүүлсэн эсэхийг шалгаарай)":
        "No room with this code (check that your friend has started a province)",
    "Найзын тань тоглоомын хувилбар өөр байна. Бүгд шинэчилнэ үү": "Your friend has a different game version. Everyone needs to update",
    "Найзтайгаа тоглохын тулд тоглоомоо шинэ хувилбараар шинэчилнэ үү": "Update the game to the latest version to play with friends",
    "Энэ таны өөрийн өрөө байна": "This is your own room",
    "Энэ өрөөнд одоо тулаан явагдаж байна": "A battle is going on in this room right now",
    "Зөвхөн хост": "Host only",
    "Тулаан дууссан": "The battle is over",
    "Хамт тоглолтод тулаан байхгүй": "There are no battles in co-op",
    "Зочин тоглоомоор холбогдоогүй: тулаан тоологдохгүй": "The guest never connected: the battle doesn't count",
    "Өрөө хаагдсан: тулаан хүчингүй": "The room is closed: battle voided",
    "Зөвхөн хост үе шатыг дуусгана": "Only the host can complete the stage",
    "Үе шат хэт хурдан дууслаа": "The stage was completed too fast",
    "Дэлгүүрийн мэдээлэл шинэчлэгдлээ. Дахин оролдоно уу": "The shop was updated. Please try again",
    "Хамгийн дээд түвшинд хүрсэн": "Maximum level reached",
    "Энэ төхөөрөмж аль хэдийн идэвхтэй байна": "This device is already active",
    "Багц буруу байна": "Invalid pack",
    "Төлбөрийн үйлчилгээ түр ажиллахгүй байна": "Payments are temporarily unavailable",
    "Энэ өрөөнд орох боломжгүй боллоо (өөр тоглогч орсон эсвэл хаагдсан). Жагсаалтаа шинэчилнэ үү":
        "You can no longer join this room (another player joined or it closed). Refresh the list",
    "Нээлттэй өрөө байгаа ч тоглоомын хувилбар нь өөр байна: хоёулаа тоглоомоо шинэ хувилбараар шинэчилнэ үү":
        "There are open rooms but with a different game version: both of you need to update",
    "Тулаан дуусаагүй байна": "The battle isn't over yet",
    "Төлбөрийн төрөл буруу байна": "Invalid payment type",
    "Нэхэмжлэх олдсонгүй": "Invoice not found",
    "Хадгалалт буруу байна": "Invalid save data",
    "Өрөө зөвхөн найзын кодоор": "Room is friend-code only",
    "Хувилбар өөр": "Different version",
}

# Тоотой мессежүүд: монгол загвар (regex) -> англи (\\1, \\2 = тоо)
PATTERNS = [
    (r"Тулаанд (\d+) Gold хэрэгтэй \(танд (\d+)\)", r"Battles need \1 Gold (you have \2)"),
    (r"Нууц үг (\d+)-аас дээш тэмдэгт байна", r"The password needs at least \1 characters"),
    (r"Хамгийн багадаа (\d+) Gold татна", r"The minimum is \1 Gold"),
    (r"Зөвхөн тоглоомоос хожсон Gold-оо татна\. Одоо (\d+) Gold татах боломжтой",
     r"Only Gold won in the game can be withdrawn. You can withdraw \1 Gold now"),
    (r"Өрөөнд орохын тулд (\d+) Gold хэрэгтэй", r"You need \1 Gold to join a room"),
    (r"Өрөө дүүрсэн байна \((\d+) тоглогч\)", r"The room is full (\1 players)"),
    (r"Одоо (\d+) Gold татах боломжтой", r"You can withdraw \1 Gold now"),
    (r"Төхөөрөмж солиход (\d+) Gold хэрэгтэй", r"You need \1 Gold to change devices"),
    (r"Энэ дүрээр тоглоход (\d+) Gold хэрэгтэй", r"You need \1 Gold to play this character"),
    (r"Таны зэрэгтэй ойролцоо \((\d+)–(\d+)\) тоглогчийн нээлттэй өрөө алга\. Дараа дахин оролдоно уу",
     r"No open rooms from players near your rank (\1–\2). Please try again later"),
    (r"Зэрэг зөрүүтэй \((\d+)–(\d+)\)", r"Rank too far apart (\1–\2)"),
]
_COMPILED = [(re.compile("^" + mn + "$"), en) for mn, en in PATTERNS]


def english(text):
    if not isinstance(text, str):
        return text
    if text in EN:
        return EN[text]
    for pattern, replacement in _COMPILED:
        if pattern.match(text):
            return pattern.sub(replacement, text)
    return text


def translate(data):
    """Хариуны JSON дотор (давхар бүтэц ч) FIELDS-ийн утгуудыг англи болгоно."""
    if isinstance(data, dict):
        return {k: english(v) if k in FIELDS else translate(v) for k, v in data.items()}
    if isinstance(data, list):
        return [translate(v) for v in data]
    return data
