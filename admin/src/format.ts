import dayjs from 'dayjs';
import relativeTime from 'dayjs/plugin/relativeTime';
import 'dayjs/locale/mn';

dayjs.extend(relativeTime);
dayjs.locale('mn');

// Тоглоомын аяны дараалал (src/CampaignData.cpp): 0-оос эхэлсэн дугаар
export const PROVINCES = [
  'Баян-Өлгий', 'Увс', 'Ховд', 'Завхан', 'Говь-Алтай', 'Хөвсгөл', 'Баянхонгор', 'Архангай', 'Булган', 'Өвөрхангай',
  'Орхон', 'Өмнөговь', 'Сэлэнгэ', 'Дархан-Уул', 'Төв', 'Улаанбаатар', 'Дундговь', 'Говьсүмбэр', 'Дорноговь', 'Хэнтий',
  'Сүхбаатар', 'Дорнод',
];

export function provinceName(index: number): string {
  return PROVINCES[index] ?? `#${index + 1}`;
}

export function money(value: number | null | undefined): string {
  return (value ?? 0).toLocaleString('en-US').replace(/,/g, ' ') + '₮';
}

export function dateTime(value: string | null | undefined): string {
  return value ? dayjs(value).format('YYYY-MM-DD HH:mm') : '—';
}

export function ago(value: string | null | undefined): string {
  return value ? dayjs(value).fromNow() : '—';
}

export const PLATFORM_LABEL: Record<string, string> = { windows: 'Windows', android: 'Android', web: 'Вэб', other: 'Бусад' };

export const SOURCE_LABEL: Record<string, { text: string; color: string }> = {
  free: { text: 'Үнэгүй', color: 'green' },
  paid: { text: 'Төлбөртэй', color: 'gold' },
  admin: { text: 'Админ', color: 'purple' },
};

export const STATUS_LABEL: Record<string, { text: string; color: string }> = {
  pending: { text: 'Хүлээгдэж буй', color: 'processing' },
  paid: { text: 'Төлөгдсөн', color: 'success' },
  cancelled: { text: 'Цуцлагдсан', color: 'default' },
};

export const KIND_LABEL: Record<string, string> = { pack: 'Аймгийн багц', device: 'Төхөөрөмж солих' };

// Лог: үйлдлийн нэр ба өнгө
export const EVENT_LABEL: Record<string, { text: string; color: string }> = {
  registered: { text: 'Бүртгүүлсэн', color: 'green' },
  login: { text: 'Нэвтэрсэн', color: 'blue' },
  login_failed: { text: 'Нэвтрэлт амжилтгүй', color: 'red' },
  device_bound: { text: 'Төхөөрөмж бүртгэсэн', color: 'geekblue' },
  logout: { text: 'Гарсан', color: 'default' },
  password_reset: { text: 'Нууц үг сэргээсэн', color: 'orange' },
  province_unlocked: { text: 'Аймаг нээсэн', color: 'gold' },
  province_start: { text: 'Аймаг эхлүүлсэн', color: 'cyan' },
  invoice_created: { text: 'Нэхэмжлэх үүсгэсэн', color: 'default' },
  invoice_failed: { text: 'Нэхэмжлэх алдаа', color: 'red' },
  payment: { text: 'Төлбөр орсон', color: 'success' },
  payment_check_failed: { text: 'Төлбөр шалгах алдаа', color: 'red' },
  device_change_conflict: { text: 'Төхөөрөмжийн зөрчил', color: 'red' },
  sms_failed: { text: 'SMS алдаа', color: 'red' },
  register_verification_started: { text: 'Бүртгэл: SMS хүлээж буй', color: 'default' },
  reset_verification_started: { text: 'Сэргээх: SMS хүлээж буй', color: 'default' },
  staff_login: { text: 'Админ нэвтэрсэн', color: 'purple' },
  staff_login_failed: { text: 'Админ нэвтрэлт амжилтгүй', color: 'red' },
};

export function eventLabel(kind: string): { text: string; color: string } {
  if (EVENT_LABEL[kind]) return EVENT_LABEL[kind];
  if (kind.startsWith('admin_')) return { text: 'Админ: ' + kind.slice(6).replace(/_/g, ' '), color: 'purple' };
  if (kind.startsWith('game_')) return { text: 'Тоглоом: ' + kind.slice(5).replace(/_/g, ' '), color: 'geekblue' };
  return { text: kind, color: 'default' };
}
