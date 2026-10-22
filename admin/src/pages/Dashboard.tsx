import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Alert, Button, Card, Col, Empty, List, Row, Segmented, Skeleton, Statistic, Tag, Typography, theme } from 'antd';
import {
  ClockCircleOutlined,
  DollarOutlined,
  ReloadOutlined,
  RiseOutlined,

  TeamOutlined,
  ThunderboltOutlined,
  UserAddOutlined,
} from '@ant-design/icons';
import {
  Area,
  AreaChart,
  Bar,
  BarChart,
  CartesianGrid,
  Cell,
  Legend,
  Pie,
  PieChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import dayjs from 'dayjs';
import { api, InvoiceRow } from '../api';
import { ago, KIND_LABEL, money, PLATFORM_LABEL, provinceName } from '../format';

interface DashboardData {
  totals: {
    players: number;
    players_today: number;
    active_24h: number;
    revenue_total: number;
    revenue_today: number;
    revenue_30d: number;
    paying_players: number;
    invoices_pending: number;
    blocked: number;
  };
  series: { day: string; registrations: number; revenue: number; active: number }[];
  provinces: { province: number; unlocked: number; starts: number }[];
  platforms: Record<string, number>;
  recent_payments: InvoiceRow[];
}

const GOLD = '#f0b030';
const PIE_COLORS = ['#f0b030', '#3b8fd9', '#52c41a', '#d8322a', '#9254de'];

function StatCard({ title, value, icon, color, suffix, footer }: { title: string; value: string | number; icon: React.ReactNode; color: string; suffix?: string; footer?: React.ReactNode }) {
  return (
    <Card className="stat-card" variant="borderless" style={{ height: '100%' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: 12 }}>
        <Statistic title={title} value={value} suffix={suffix} valueStyle={{ fontWeight: 650 }} />
        <div
          style={{
            width: 44,
            height: 44,
            flex: 'none',
            borderRadius: 12,
            display: 'grid',
            placeItems: 'center',
            fontSize: 20,
            color,
            background: color + '1f',
          }}
        >
          {icon}
        </div>
      </div>
      {footer && <div style={{ marginTop: 8, fontSize: 13, opacity: 0.65 }}>{footer}</div>}
    </Card>
  );
}

export default function DashboardPage() {
  const [data, setData] = useState<DashboardData | null>(null);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const [provinceMetric, setProvinceMetric] = useState<'unlocked' | 'starts'>('unlocked');
  const { token } = theme.useToken();

  const load = useCallback(() => {
    setLoading(true);
    api<DashboardData>('dashboard/')
      .then((r) => {
        setData(r);
        setError('');
      })
      .catch((e) => setError(e.message))
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    load();
    const timer = window.setInterval(load, 60000);
    return () => window.clearInterval(timer);
  }, [load]);

  const axis = { stroke: token.colorTextTertiary, fontSize: 12 };
  const tooltipStyle = {
    contentStyle: { background: token.colorBgElevated, border: `1px solid ${token.colorBorderSecondary}`, borderRadius: 8 },
    labelStyle: { color: token.colorText },
  };

  const t = data?.totals;
  const series = (data?.series ?? []).map((row) => ({ ...row, label: dayjs(row.day).format('MM/DD') }));
  const provinces = (data?.provinces ?? [])
    .map((row) => ({ ...row, name: provinceName(row.province) }))
    .sort((a, b) => b[provinceMetric] - a[provinceMetric]);
  const platforms = Object.entries(data?.platforms ?? {}).map(([key, value]) => ({ name: PLATFORM_LABEL[key] ?? key, value }));

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Хянах самбар</h1>
          <p>Тоглогч, орлого, идэвхийн тойм · сүүлийн 30 хоног</p>
        </div>
        <Button icon={<ReloadOutlined />} onClick={load} loading={loading}>
          Шинэчлэх
        </Button>
      </div>
      {error && <Alert type="error" message={error} showIcon style={{ marginBottom: 16 }} />}
      {!data ? (
        <Skeleton active paragraph={{ rows: 10 }} />
      ) : (
        <>
          <Row gutter={[16, 16]}>
            <Col xs={24} sm={12} xl={6}>
              <StatCard title="Нийт тоглогч" value={t!.players} icon={<TeamOutlined />} color="#3b8fd9" footer={<>Өнөөдөр <b>+{t!.players_today}</b> шинэ</>} />
            </Col>
            <Col xs={24} sm={12} xl={6}>
              <StatCard title="Идэвхтэй (24 цаг)" value={t!.active_24h} icon={<ThunderboltOutlined />} color="#52c41a" footer={<>Хаагдсан: {t!.blocked}</>} />
            </Col>
            <Col xs={24} sm={12} xl={6}>
              <StatCard title="Орлого (30 хоног)" value={money(t!.revenue_30d)} icon={<RiseOutlined />} color={GOLD} footer={<>Өнөөдөр <b>{money(t!.revenue_today)}</b></>} />
            </Col>
            <Col xs={24} sm={12} xl={6}>
              <StatCard
                title="Нийт орлого"
                value={money(t!.revenue_total)}
                icon={<DollarOutlined />}
                color="#d8322a"
                footer={
                  <>
                    Төлсөн тоглогч <b>{t!.paying_players}</b> · хүлээгдэж буй <b>{t!.invoices_pending}</b>
                  </>
                }
              />
            </Col>
          </Row>

          <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
            <Col xs={24} xl={16}>
              <Card title="Орлого" variant="borderless" extra={<Typography.Text type="secondary">₮ / өдөр</Typography.Text>}>
                <ResponsiveContainer width="100%" height={280}>
                  <AreaChart data={series} margin={{ left: 8, right: 8, top: 8 }}>
                    <defs>
                      <linearGradient id="revenue" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="0%" stopColor={GOLD} stopOpacity={0.45} />
                        <stop offset="100%" stopColor={GOLD} stopOpacity={0} />
                      </linearGradient>
                    </defs>
                    <CartesianGrid strokeDasharray="3 3" stroke={token.colorBorderSecondary} vertical={false} />
                    <XAxis dataKey="label" {...axis} tickLine={false} />
                    <YAxis {...axis} tickLine={false} axisLine={false} width={64} tickFormatter={(v) => (v >= 1000 ? `${v / 1000}к` : v)} />
                    <Tooltip {...tooltipStyle} formatter={(v: number) => [money(v), 'Орлого']} />
                    <Area type="monotone" dataKey="revenue" stroke={GOLD} strokeWidth={2} fill="url(#revenue)" />
                  </AreaChart>
                </ResponsiveContainer>
              </Card>
            </Col>
            <Col xs={24} xl={8}>
              <Card title="Сүүлийн төлбөрүүд" variant="borderless" extra={<Link to="/invoices">Бүгд</Link>} style={{ height: '100%' }}>
                {data.recent_payments.length === 0 ? (
                  <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Төлбөр алга" />
                ) : (
                  <List
                    size="small"
                    dataSource={data.recent_payments.slice(0, 6)}
                    renderItem={(item) => (
                      <List.Item extra={<b style={{ color: GOLD }}>{money(item.amount)}</b>}>
                        <List.Item.Meta
                          title={<Link to={`/players/${item.player.id}`}>{item.player.name}</Link>}
                          description={
                            <>
                              {KIND_LABEL[item.kind]} · <ClockCircleOutlined /> {ago(item.paid_at)}
                            </>
                          }
                        />
                      </List.Item>
                    )}
                  />
                )}
              </Card>
            </Col>
          </Row>

          <Row gutter={[16, 16]} style={{ marginTop: 16 }}>
            <Col xs={24} xl={16}>
              <Card title="Бүртгэл ба идэвх" variant="borderless" extra={<UserAddOutlined />}>
                <ResponsiveContainer width="100%" height={260}>
                  <BarChart data={series} margin={{ left: 8, right: 8, top: 8 }}>
                    <CartesianGrid strokeDasharray="3 3" stroke={token.colorBorderSecondary} vertical={false} />
                    <XAxis dataKey="label" {...axis} tickLine={false} />
                    <YAxis {...axis} tickLine={false} axisLine={false} allowDecimals={false} width={40} />
                    <Tooltip {...tooltipStyle} cursor={{ fill: token.colorFillTertiary }} />
                    <Legend />
                    <Bar dataKey="active" name="Идэвхтэй тоглогч" fill="#3b8fd9" radius={[4, 4, 0, 0]} />
                    <Bar dataKey="registrations" name="Шинэ бүртгэл" fill="#52c41a" radius={[4, 4, 0, 0]} />
                  </BarChart>
                </ResponsiveContainer>
              </Card>
            </Col>
            <Col xs={24} xl={8}>
              <Card title="Платформ" variant="borderless" style={{ height: '100%' }}>
                {platforms.length === 0 ? (
                  <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description="Төхөөрөмж алга" />
                ) : (
                  <ResponsiveContainer width="100%" height={260}>
                    <PieChart>
                      <Pie data={platforms} dataKey="value" nameKey="name" innerRadius={60} outerRadius={95} paddingAngle={3} stroke="none">
                        {platforms.map((_, i) => (
                          <Cell key={i} fill={PIE_COLORS[i % PIE_COLORS.length]} />
                        ))}
                      </Pie>
                      <Tooltip {...tooltipStyle} />
                      <Legend />
                    </PieChart>
                  </ResponsiveContainer>
                )}
              </Card>
            </Col>
          </Row>

          <Card
            title="Аймгууд"
            variant="borderless"
            style={{ marginTop: 16 }}
            extra={
              <Segmented
                size="small"
                value={provinceMetric}
                onChange={(v) => setProvinceMetric(v as 'unlocked' | 'starts')}
                options={[
                  { label: 'Нээсэн', value: 'unlocked' },
                  { label: 'Тоглосон', value: 'starts' },
                ]}
              />
            }
          >
            <ResponsiveContainer width="100%" height={320}>
              <BarChart data={provinces} margin={{ left: 8, right: 8, top: 8, bottom: 40 }}>
                <CartesianGrid strokeDasharray="3 3" stroke={token.colorBorderSecondary} vertical={false} />
                <XAxis dataKey="name" {...axis} tickLine={false} interval={0} angle={-40} textAnchor="end" height={60} />
                <YAxis {...axis} tickLine={false} axisLine={false} allowDecimals={false} width={40} />
                <Tooltip {...tooltipStyle} cursor={{ fill: token.colorFillTertiary }} />
                <Bar dataKey={provinceMetric} name={provinceMetric === 'unlocked' ? 'Нээсэн тоглогч' : 'Эхлүүлсэн удаа'} radius={[4, 4, 0, 0]}>
                  {provinces.map((row) => (
                    <Cell key={row.province} fill={row.province === 15 ? '#d8322a' : GOLD} />
                  ))}
                </Bar>
              </BarChart>
            </ResponsiveContainer>
            <Typography.Text type="secondary">
              <Tag color="red">Улаанбаатар</Tag> үргэлж төлбөртэй
            </Typography.Text>
          </Card>
        </>
      )}
    </>
  );
}
