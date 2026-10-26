// Django админ API (/api/manage/): токен localStorage-д, 401 бол нэвтрэх хуудас руу.

const BASE = '/api/manage/';
const TOKEN_KEY = 'zm_admin_token';

export class ApiError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

let onUnauthorized: (() => void) | null = null;
export function setUnauthorizedHandler(handler: () => void) {
  onUnauthorized = handler;
}

export function getToken(): string {
  try {
    return localStorage.getItem(TOKEN_KEY) || '';
  } catch {
    return '';
  }
}

export function setToken(token: string) {
  try {
    if (token) localStorage.setItem(TOKEN_KEY, token);
    else localStorage.removeItem(TOKEN_KEY);
  } catch {
    /* хувийн цонх */
  }
}

export async function api<T = any>(path: string, options: { method?: string; body?: unknown; query?: Record<string, unknown> } = {}): Promise<T> {
  let url = BASE + path;
  if (options.query) {
    const params = new URLSearchParams();
    Object.entries(options.query).forEach(([key, value]) => {
      if (value !== undefined && value !== null && value !== '') params.set(key, String(value));
    });
    const text = params.toString();
    if (text) url += '?' + text;
  }
  const headers: Record<string, string> = { 'Content-Type': 'application/json' };
  const token = getToken();
  if (token) headers.Authorization = 'Bearer ' + token;
  let response: Response;
  try {
    response = await fetch(url, {
      method: options.method || 'GET',
      headers,
      body: options.body !== undefined ? JSON.stringify(options.body) : undefined,
    });
  } catch {
    throw new ApiError('Сервертэй холбогдож чадсангүй', 0);
  }
  let data: any = null;
  try {
    data = await response.json();
  } catch {
    data = null;
  }
  if (!response.ok) {
    if (response.status === 401 && data?.error === 'auth') {
      setToken('');
      onUnauthorized?.();
    }
    throw new ApiError(data?.message || `Алдаа (${response.status})`, response.status);
  }
  return data as T;
}

// ---- Төрлүүд

export interface PlayerRow {
  id: number;
  name: string;
  phone: string;
  verified: boolean;
  free_used: number;
  credits: number;
  gold: number;
  gold_locked: number;
  gold_won: number;
  gold_bonus?: number;
  blocked: boolean;
  created: string;
  provinces: number | null;
  paid_total: number;
  last_seen: string | null;
}

export interface InvoiceRow {
  id: number;
  player: { id: number; name: string; phone: string };
  kind: 'pack' | 'device' | 'capital' | 'gold';
  amount: number;
  credits: number;
  gold: number;
  status: 'pending' | 'paid' | 'cancelled';
  applied: boolean;
  platform: string;
  qpay_invoice_id: string;
  qpay_payment_id: string;
  created: string;
  paid_at: string | null;
}

export interface EventRow {
  id: number;
  player: { id: number; name: string; phone: string } | null;
  kind: string;
  data: Record<string, unknown>;
  device_id: string;
  ip: string | null;
  created: string;
}

export interface PlayerDetail extends PlayerRow {
  free_left: number;
  progress_updated: string | null;
  has_progress: boolean;
  unlocked: { province: number; source: 'free' | 'paid' | 'admin'; created: string }[];
  devices: { id: number; device_id: string; platform: string; active: boolean; bound_at: string }[];
  sessions: number;
  invoices: InvoiceRow[];
  upgrades: Record<'damage' | 'fire_rate' | 'magazine' | 'health' | 'soldier', number>;
  gold_history: { amount: number; balance: number; reason: string; data: Record<string, unknown>; created: string }[];
}

export interface Page<T> {
  items: T[];
  total: number;
  page: number;
  size: number;
}

export interface Settings {
  free_provinces: number;
  pack_size: number;
  pack_price: number;
  device_change_price: number;
  province_count: number;
  capital_index: number;
  max_devices: number;
  capital_price: number;
  start_gold: number;
  price_damage: number;
  price_fire_rate: number;
  price_magazine: number;
  price_health: number;
  price_soldier: number;
  skin_price: number;
  skin_price_iron: number;
  coop_reward: number;
  coop_daily_limit: number;
  room_fee: number;
  room_stake: number;
  stage_bonus: number;
  stage_min_minutes: number;
  battle_pair_limit: number;
  stage_daily_limit: number;
  verify_bonus: number;
  register_per_ip_day: number;
  block_same_ip: boolean;
  gold_packs: { gold: number; price: number }[];
  cashout_enabled: boolean;
  cashout_min: number;
  cashout_rate: number;
}

export interface CashoutRow {
  id: number;
  player: { id: number; name: string; phone: string; won: number };
  gold: number;
  amount: number;
  rate: number;
  bank: string;
  account: string;
  holder: string;
  status: 'verifying' | 'pending' | 'paid' | 'rejected' | 'expired' | 'cancelled';
  note: string;
  handled_by: string;
  created: string;
  verified_at: string | null;
  closed_at: string | null;
}

export interface RoomRow {
  id: number;
  province: number;
  status: 'open' | 'closed';
  phase: 'mission' | 'battle' | 'completed' | 'closed';
  host: { id: number; name: string; phone: string };
  guest: { id: number; name: string; phone: string } | null;
  battle: number;
  host_stake: number;
  guest_stake: number;
  recruits: number;
  closed_reason: string;
  created: string;
  closed: string | null;
  host_seen: string;
}

export interface StaffUser {
  username: string;
  superuser: boolean;
}
