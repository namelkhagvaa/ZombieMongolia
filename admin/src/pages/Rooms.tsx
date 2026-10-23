import { useCallback, useEffect, useState } from 'react';
import { Button, Card, Select, Space, Table, Tag, Tooltip, Typography } from 'antd';
import type { TableProps } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { api, Page, RoomRow } from '../api';
import { ago, dateTime, provinceName } from '../format';

const REASON: Record<string, string> = {
  completed: 'Үе шат дууссан',
  host_left: 'Хост гарсан',
  timeout: 'Холболт тасарсан',
  replaced: 'Шинээр эхэлсэн',
};

// Нээлттэй өрөөнүүд: хост, зочин, тулааны дугаар, барьцаа (Gold-той тоглолт)
export default function RoomsPage() {
  const [data, setData] = useState<Page<RoomRow> | null>(null);
  const [loading, setLoading] = useState(false);
  const [query, setQuery] = useState({ status: '', page: 1, size: 20 });

  const load = useCallback(() => {
    setLoading(true);
    api<Page<RoomRow>>('rooms/', { query })
      .then(setData)
      .catch(() => setData(null))
      .finally(() => setLoading(false));
  }, [query]);

  useEffect(load, [load]);

  const person = (p: RoomRow['host'] | null, stake: number) =>
    p ? (
      <Space direction="vertical" size={0}>
        <Typography.Text strong>{p.name}</Typography.Text>
        <Typography.Text type="secondary" className="mono">
          {p.phone} {stake ? `· ${stake} барьцаа` : ''}
        </Typography.Text>
      </Space>
    ) : (
      <Typography.Text type="secondary">—</Typography.Text>
    );

  const columns: TableProps<RoomRow>['columns'] = [
    { title: '#', dataIndex: 'id', width: 70 },
    { title: 'Аймаг', dataIndex: 'province', render: (value) => provinceName(value) },
    {
      title: 'Төлөв',
      dataIndex: 'status',
      render: (value, row) =>
        value === 'open' ? <Tag color="green">Нээлттэй</Tag> : <Tag>{REASON[row.closed_reason] ?? row.closed_reason ?? 'Хаагдсан'}</Tag>,
    },
    { title: 'Хост', dataIndex: 'host', render: (_, row) => person(row.host, row.host_stake) },
    { title: 'Зочин', dataIndex: 'guest', render: (_, row) => person(row.guest, row.guest_stake) },
    { title: 'Тулаан', dataIndex: 'battle', align: 'center', width: 80 },
    { title: 'Цэрэг', dataIndex: 'recruits', align: 'center', width: 80, responsive: ['md'] },
    {
      title: 'Нээсэн',
      dataIndex: 'created',
      width: 150,
      render: (value, row) => (
        <Tooltip title={`${dateTime(value)}${row.closed ? ' — ' + dateTime(row.closed) : ''}`}>
          <span>{ago(value)}</span>
        </Tooltip>
      ),
    },
  ];

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Өрөө, тулаан</h1>
          <p>{data ? `${data.total} өрөө` : 'Нээлттэй өрөөнүүд (Gold-той тоглолт)'}</p>
        </div>
        <Space>
          <Select
            value={query.status}
            style={{ width: 160 }}
            onChange={(status) => setQuery({ ...query, status, page: 1 })}
            options={[
              { value: '', label: 'Бүгд' },
              { value: 'open', label: 'Нээлттэй' },
              { value: 'closed', label: 'Хаагдсан' },
            ]}
          />
          <Button icon={<ReloadOutlined />} onClick={load} loading={loading}>
            Шинэчлэх
          </Button>
        </Space>
      </div>
      <Card variant="borderless">
        <Table
          rowKey="id"
          columns={columns}
          dataSource={data?.items ?? []}
          loading={loading}
          scroll={{ x: 800 }}
          pagination={{
            current: query.page,
            pageSize: query.size,
            total: data?.total ?? 0,
            onChange: (page, size) => setQuery({ ...query, page, size }),
          }}
        />
      </Card>
    </>
  );
}
