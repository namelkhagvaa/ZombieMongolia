import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Button, Input, Select, Space, Table, Tag, Tooltip, Typography } from 'antd';
import type { TableProps } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { api, EventRow, Page } from '../api';
import { ago, dateTime, eventLabel, provinceName } from '../format';

// Логийн мэдээллийг уншигдахуйц болгоно: аймгийн дугаарыг нэрээр
function describe(data: Record<string, unknown>): string {
  return Object.entries(data)
    .map(([key, value]) => {
      if (key === 'province' && typeof value === 'number') return `аймаг: ${provinceName(value)}`;
      if (typeof value === 'object') return `${key}: ${JSON.stringify(value)}`;
      return `${key}: ${value}`;
    })
    .join(' · ');
}

export default function EventTable({ playerId, compact }: { playerId?: number; compact?: boolean }) {
  const [data, setData] = useState<Page<EventRow> | null>(null);
  const [loading, setLoading] = useState(false);
  const [kinds, setKinds] = useState<string[]>([]);
  const [query, setQuery] = useState({ kind: '', q: '', page: 1, size: compact ? 10 : 30 });

  const load = useCallback(() => {
    setLoading(true);
    api<Page<EventRow>>('events/', { query: { ...query, player: playerId } })
      .then(setData)
      .catch(() => setData(null))
      .finally(() => setLoading(false));
  }, [query, playerId]);

  useEffect(load, [load]);
  useEffect(() => {
    api<{ kinds: string[] }>('events/kinds/')
      .then((r) => setKinds(r.kinds))
      .catch(() => setKinds([]));
  }, []);

  const columns: TableProps<EventRow>['columns'] = [
    {
      title: 'Цаг',
      dataIndex: 'created',
      width: 150,
      render: (v) => (
        <Tooltip title={dateTime(v)}>
          <span>{ago(v)}</span>
        </Tooltip>
      ),
    },
    ...(playerId
      ? []
      : [
          {
            title: 'Тоглогч',
            dataIndex: 'player',
            width: 180,
            render: (p: EventRow['player']) =>
              p ? <Link to={`/players/${p.id}`}>{p.name}</Link> : <Typography.Text type="secondary">—</Typography.Text>,
          },
        ]),
    {
      title: 'Үйлдэл',
      dataIndex: 'kind',
      width: 210,
      render: (kind) => {
        const label = eventLabel(kind);
        return <Tag color={label.color}>{label.text}</Tag>;
      },
    },
    {
      title: 'Дэлгэрэнгүй',
      dataIndex: 'data',
      render: (d: Record<string, unknown>) => (
        <Typography.Text type="secondary" style={{ fontSize: 12 }} ellipsis={{ tooltip: JSON.stringify(d, null, 2) }}>
          {describe(d) || '—'}
        </Typography.Text>
      ),
    },
    {
      title: 'IP',
      dataIndex: 'ip',
      width: 130,
      responsive: ['lg'],
      render: (v) => <span className="mono">{v || '—'}</span>,
    },
  ];

  return (
    <>
      <Space wrap style={{ marginBottom: 12 }}>
        <Select
          showSearch
          allowClear
          placeholder="Бүх үйлдэл"
          style={{ width: 240 }}
          value={query.kind || undefined}
          onChange={(kind) => setQuery({ ...query, kind: kind ?? '', page: 1 })}
          options={[
            { value: 'admin_', label: 'Админы бүх үйлдэл' },
            { value: 'game_', label: 'Тоглоомын үйл явдал' },
            ...kinds.map((k) => ({ value: k, label: eventLabel(k).text })),
          ]}
          optionFilterProp="label"
        />
        {!playerId && (
          <Input.Search
            allowClear
            placeholder="Тоглогч, утас, IP, төхөөрөмж"
            style={{ width: 260 }}
            onSearch={(q) => setQuery({ ...query, q, page: 1 })}
          />
        )}
        <Button icon={<ReloadOutlined />} onClick={load} />
      </Space>
      <Table<EventRow>
        rowKey="id"
        size={compact ? 'small' : 'middle'}
        loading={loading}
        columns={columns}
        dataSource={data?.items}
        scroll={{ x: 720 }}
        pagination={{
          current: query.page,
          pageSize: query.size,
          total: data?.total,
          showSizeChanger: !compact,
          showTotal: (n) => `Нийт ${n}`,
        }}
        onChange={(p) => setQuery({ ...query, page: p.current ?? 1, size: p.pageSize ?? query.size })}
      />
    </>
  );
}
