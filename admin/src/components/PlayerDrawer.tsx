import { useCallback, useEffect, useState } from 'react';
import {
  App,
  Button,
  Card,
  Col,
  Descriptions,
  Drawer,
  Empty,
  Form,
  Grid,
  Input,
  InputNumber,
  List,
  Modal,
  Popconfirm,
  Row,
  Skeleton,
  Space,
  Statistic,
  Switch,
  Tabs,
  Tag,
  Tooltip,
  Typography,
} from 'antd';
import {
  DeleteOutlined,
  DesktopOutlined,
  KeyOutlined,
  LockOutlined,
  LogoutOutlined,
  MobileOutlined,
  GlobalOutlined,
} from '@ant-design/icons';
import { api, PlayerDetail } from '../api';
import { ago, dateTime, GOLD_REASON, PLATFORM_LABEL, money } from '../format';
import InvoiceTable from './InvoiceTable';
import EventTable from './EventTable';

const PLATFORM_ICON: Record<string, React.ReactNode> = {
  windows: <DesktopOutlined />,
  android: <MobileOutlined />,
  web: <GlobalOutlined />,
};

export default function PlayerDrawer({ playerId, onClose, onChanged }: { playerId: number | null; onClose: () => void; onChanged: () => void }) {
  const { message } = App.useApp();
  const screens = Grid.useBreakpoint();
  const [player, setPlayer] = useState<PlayerDetail | null>(null);
  const [busy, setBusy] = useState(false);
  const [gold, setGold] = useState<number | null>(null);
  const [passwordOpen, setPasswordOpen] = useState(false);
  const [form] = Form.useForm();

  const load = useCallback(() => {
    if (playerId == null) return;
    api<{ player: PlayerDetail }>(`players/${playerId}/`)
      .then((r) => setPlayer(r.player))
      .catch((e) => {
        message.error(e.message);
        onClose();
      });
  }, [playerId, message, onClose]);

  useEffect(() => {
    setPlayer(null);
    load();
  }, [load]);

  useEffect(() => {
    setGold(player?.gold ?? null);
  }, [player]);

  const act = async (path: string, method: string, body: unknown, done: string) => {
    setBusy(true);
    try {
      const r = await api<{ player: PlayerDetail }>(`players/${playerId}/${path}`, { method, body });
      setPlayer(r.player);
      message.success(done);
      onChanged();
      return true;
    } catch (e) {
      message.error((e as Error).message);
      return false;
    } finally {
      setBusy(false);
    }
  };


  return (
    <Drawer
      open={playerId != null}
      onClose={onClose}
      width={screens.md ? 760 : '100%'}
      title={
        player ? (
          <Space>
            {player.name}
            {player.blocked && <Tag color="red">Хаагдсан</Tag>}
          </Space>
        ) : (
          'Тоглогч'
        )
      }
      extra={
        player && (
          <Space>
            <Tooltip title="Бүх төхөөрөмжөөс гаргана">
              <Popconfirm title="Бүх төхөөрөмжөөс гаргах уу?" onConfirm={() => act('logout/', 'POST', {}, 'Гаргалаа')}>
                <Button icon={<LogoutOutlined />} disabled={!player.sessions}>
                  Гаргах ({player.sessions})
                </Button>
              </Popconfirm>
            </Tooltip>
            <Button icon={<KeyOutlined />} onClick={() => setPasswordOpen(true)}>
              Нууц үг
            </Button>
          </Space>
        )
      }
    >
      {!player ? (
        <Skeleton active paragraph={{ rows: 8 }} />
      ) : (
        <>
          <Row gutter={[12, 12]}>
            <Col xs={12} sm={6}>
              <Card size="small">
                <Statistic
                  title="Gold"
                  value={player.gold}
                  valueStyle={{ color: '#f0b030' }}
                  suffix={player.gold_locked ? <span style={{ fontSize: 13 }}>{`(${player.gold_locked} барьцаанд)`}</span> : undefined}
                />
              </Card>
            </Col>
            <Col xs={24} sm={12}>
              <Card size="small">
                <Statistic
                  title="Сайжруулалт (хохирол · хурд · хайрцаг · амь · цэрэг)"
                  value={`${player.upgrades.damage} · ${player.upgrades.fire_rate} · ${player.upgrades.magazine} · ${player.upgrades.health} · ${player.upgrades.soldier}`}
                />
              </Card>
            </Col>
            <Col xs={12} sm={6}>
              <Card size="small">
                <Statistic title="Нийт төлсөн" value={money(player.paid_total)} valueStyle={{ color: '#f0b030' }} />
              </Card>
            </Col>
          </Row>

          <Descriptions size="small" column={screens.sm ? 2 : 1} style={{ marginTop: 16 }} bordered>
            <Descriptions.Item label="Утас">
              <Typography.Text copyable className="mono">
                {player.phone}
              </Typography.Text>
            </Descriptions.Item>
            <Descriptions.Item label="Дугаар">#{player.id}</Descriptions.Item>
            <Descriptions.Item label="Бүртгүүлсэн">{dateTime(player.created)}</Descriptions.Item>
            <Descriptions.Item label="Сүүлд идэвхтэй">{ago(player.last_seen)}</Descriptions.Item>
            <Descriptions.Item label="Хадгалалт">{player.has_progress ? dateTime(player.progress_updated) : '—'}</Descriptions.Item>
            <Descriptions.Item label="Хаах">
              <Popconfirm
                title={player.blocked ? 'Хаалтыг цуцлах уу?' : 'Тоглогчийг хаах уу? (бүх төхөөрөмжөөс гарна)'}
                onConfirm={() => act('', 'PATCH', { blocked: !player.blocked }, player.blocked ? 'Нээлээ' : 'Хаалаа')}
              >
                <Switch checked={player.blocked} loading={busy} checkedChildren={<LockOutlined />} />
              </Popconfirm>
            </Descriptions.Item>
          </Descriptions>

          <Card size="small" title="Gold засах" style={{ marginTop: 16 }}>
            <Space wrap>
              <span>Gold</span>
              <InputNumber min={0} max={10000000} value={gold} onChange={setGold} />
              <Button
                type="primary"
                loading={busy}
                disabled={gold === player.gold}
                onClick={() => act('', 'PATCH', { gold: gold ?? 0 }, 'Хадгаллаа')}
              >
                Хадгалах
              </Button>
              <Typography.Text type="secondary">Зөрүү нь «Админ» гүйлгээгээр бүртгэгдэнэ.</Typography.Text>
            </Space>
          </Card>

          <Tabs
            style={{ marginTop: 16 }}
            items={[
              {
                key: 'gold',
                label: `Gold (${player.gold_history.length})`,
                children:
                  player.gold_history.length === 0 ? (
                    <Empty description="Гүйлгээ алга" />
                  ) : (
                    <List
                      size="small"
                      dataSource={player.gold_history}
                      renderItem={(t) => (
                        <List.Item extra={<span className="mono">{t.balance.toLocaleString()}</span>}>
                          <List.Item.Meta
                            title={
                              <Space>
                                <b style={{ color: t.amount >= 0 ? '#52c41a' : '#ff4d4f' }}>
                                  {t.amount >= 0 ? '+' : ''}
                                  {t.amount.toLocaleString()}
                                </b>
                                <Tag>{GOLD_REASON[t.reason] ?? t.reason}</Tag>
                                {typeof t.data.item === 'string' && <span>{`${t.data.item} → ${t.data.level}`}</span>}
                              </Space>
                            }
                            description={dateTime(t.created)}
                          />
                        </List.Item>
                      )}
                    />
                  ),
              },
              {
                key: 'devices',
                label: `Төхөөрөмж (${player.devices.length})`,
                children:
                  player.devices.length === 0 ? (
                    <Empty description="Төхөөрөмж алга" />
                  ) : (
                    <List
                      dataSource={player.devices}
                      renderItem={(d) => (
                        <List.Item
                          actions={[
                            <Popconfirm
                              key="release"
                              title="Төхөөрөмжийг суллах уу?"
                              description="Тэр төхөөрөмжөөс шинэ данс нээх боломжтой болно."
                              onConfirm={() => act(`devices/${d.id}/release/`, 'POST', {}, 'Сууллаа')}
                            >
                              <Button danger size="small" icon={<DeleteOutlined />}>
                                Суллах
                              </Button>
                            </Popconfirm>,
                          ]}
                        >
                          <List.Item.Meta
                            avatar={<span style={{ fontSize: 22 }}>{PLATFORM_ICON[d.platform] ?? <DesktopOutlined />}</span>}
                            title={
                              <Space>
                                {PLATFORM_LABEL[d.platform] ?? (d.platform || 'Тодорхойгүй')}
                                {d.active ? <Tag color="green">Идэвхтэй</Tag> : <Tag>Өмнөх</Tag>}
                              </Space>
                            }
                            description={
                              <>
                                <span className="mono">{d.device_id}</span> · {dateTime(d.bound_at)}
                              </>
                            }
                          />
                        </List.Item>
                      )}
                    />
                  ),
              },
              {
                key: 'invoices',
                label: `Төлбөр (${player.invoices.length})`,
                children: <InvoiceTable items={player.invoices} compact onChanged={load} />,
              },
              { key: 'events', label: 'Лог', children: <EventTable playerId={player.id} compact /> },
            ]}
          />
        </>
      )}

      <Modal
        title="Шинэ нууц үг тавих"
        open={passwordOpen}
        okText="Хадгалах"
        cancelText="Болих"
        confirmLoading={busy}
        onCancel={() => setPasswordOpen(false)}
        onOk={() => form.submit()}
        destroyOnHidden
      >
        <Typography.Paragraph type="secondary">Тоглогч бүх төхөөрөмжөөс гарч, шинэ нууц үгээр нэвтэрнэ.</Typography.Paragraph>
        <Form
          form={form}
          layout="vertical"
          preserve={false}
          onFinish={async (values) => {
            if (await act('password/', 'POST', { password: values.password }, 'Нууц үг солигдлоо')) setPasswordOpen(false);
          }}
        >
          <Form.Item name="password" label="Нууц үг" rules={[{ required: true, min: 6, message: '6-аас дээш тэмдэгт' }]}>
            <Input.Password autoFocus />
          </Form.Item>
        </Form>
      </Modal>
    </Drawer>
  );
}
