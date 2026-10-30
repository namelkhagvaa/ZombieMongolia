import { useCallback, useEffect, useMemo, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import { App as AntApp, Button, Card, Checkbox, Empty, Input, Select, Space, Table, Tag, Typography } from 'antd';
import type { TableProps } from 'antd';
import { CopyOutlined, ReloadOutlined } from '@ant-design/icons';
import { api } from '../api';
import { dateTime } from '../format';

interface LogRow {
  id: number;
  at: string;
  room: number | null;
  battle: number;
  role: string;
  who: string;
  level: string;
  step: string;
  text: string;
}

interface LogResult {
  rows: LogRow[];
  text: string;
  player: string | null;
}

const ROLE_COLOR: Record<string, string> = { h: 'gold', g: 'geekblue', s: 'purple' };

// Тоглолтын лог (backend/game/gamelog.py): өрөө эсвэл тоглогчоор хост, зочин, серверийн мөрүүд нэг цагийн дарааллаар.
// "Текстээр хуулах": нэгтгэсэн цагийн хуваарийг (manage.py game_log-тэй ижил) хуулж шинжлүүлэхэд.
export default function GameLogsPage() {
  const { message } = AntApp.useApp();
  const [params, setParams] = useSearchParams();
  const [room, setRoom] = useState(params.get('room') ?? '');
  const [player, setPlayer] = useState(params.get('player') ?? '');
  const [hours, setHours] = useState(Number(params.get('hours') ?? 6));
  const [steps, setSteps] = useState(params.get('steps') === '1');
  const [data, setData] = useState<LogResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  // Хүснэгтэд сүүлд үүссэн нь эхэндээ (хуулах текст цагийн дарааллаараа үлдэнэ)
  const newestFirst = useMemo(() => [...(data?.rows ?? [])].reverse(), [data]);

  const load = useCallback(() => {
    setLoading(true);
    setError('');
    api<LogResult>('logs/', { query: { room, player, hours, steps: steps ? 1 : '' } })
      .then(setData)
      .catch((reason) => {
        setData(null);
        setError(reason.message);
      })
      .finally(() => setLoading(false));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [params]);

  useEffect(load, [load]);

  const apply = () => {
    const next: Record<string, string> = {};
    if (room.trim()) next.room = room.trim();
    if (player.trim()) next.player = player.trim();
    if (hours !== 6) next.hours = String(hours);
    if (steps) next.steps = '1';
    setParams(next);
  };

  const copy = () => {
    if (!data?.text) return;
    navigator.clipboard
      .writeText(data.text)
      .then(() => message.success('Хуулагдлаа'))
      .catch(() => message.error('Хуулж чадсангүй'));
  };

  const columns: TableProps<LogRow>['columns'] = [
    { title: 'Цаг', dataIndex: 'at', width: 160, render: (value) => <span className="mono">{dateTime(value)}</span> },
    { title: 'Өрөө', dataIndex: 'room', width: 70, render: (value) => value ?? '—' },
    {
      title: 'Хэн',
      dataIndex: 'who',
      width: 260,
      render: (value, row) => <Tag color={ROLE_COLOR[row.role]}>{value}</Tag>,
    },
    {
      title: 'Мөр',
      dataIndex: 'text',
      render: (value, row) => (
        <Space size={6} align="start">
          {row.step ? <Tag color="green">{row.step}</Tag> : null}
          {row.level === 'E' ? <Tag color="red">алдаа</Tag> : row.level === 'W' ? <Tag color="orange">анхаар</Tag> : null}
          <Typography.Text className="mono" style={{ whiteSpace: 'pre-wrap' }}>
            {row.battle && row.role === 's' ? `[тулаан ${row.battle}] ${value}` : value}
          </Typography.Text>
        </Space>
      ),
    },
  ];

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Тоглолтын лог</h1>
          <p>
            {data
              ? `${data.rows.length} мөр${data.player ? ` · ${data.player}` : ''}`
              : 'Тулаан, хамтын тоглолт, сүлжээ, алдаа: хост, зочин, сервер нэг дарааллаар'}
          </p>
        </div>
        <Space wrap>
          <Input placeholder="Өрөө №" value={room} onChange={(e) => setRoom(e.target.value)} onPressEnter={apply} style={{ width: 100 }} />
          <Input
            placeholder="Тоглогч (нэр, утас)"
            value={player}
            onChange={(e) => setPlayer(e.target.value)}
            onPressEnter={apply}
            style={{ width: 180 }}
          />
          <Select
            value={hours}
            style={{ width: 130 }}
            onChange={setHours}
            options={[
              { value: 1, label: 'Сүүлийн 1 цаг' },
              { value: 6, label: 'Сүүлийн 6 цаг' },
              { value: 24, label: 'Сүүлийн 24 цаг' },
              { value: 72, label: 'Сүүлийн 3 өдөр' },
              { value: 336, label: 'Сүүлийн 14 өдөр' },
            ]}
          />
          <Checkbox checked={steps} onChange={(e) => setSteps(e.target.checked)}>
            Зөвхөн алхмууд
          </Checkbox>
          <Button type="primary" onClick={apply}>
            Харах
          </Button>
          <Button icon={<ReloadOutlined />} onClick={load} loading={loading} />
          <Button icon={<CopyOutlined />} onClick={copy} disabled={!data?.text}>
            Текстээр хуулах
          </Button>
        </Space>
      </div>
      <Card variant="borderless">
        {error ? (
          <Empty description={error} />
        ) : (
          <Table
            rowKey="id"
            size="small"
            columns={columns}
            dataSource={newestFirst}
            loading={loading}
            scroll={{ x: 900 }}
            pagination={{ pageSize: 200, showSizeChanger: false }}
          />
        )}
      </Card>
    </>
  );
}
