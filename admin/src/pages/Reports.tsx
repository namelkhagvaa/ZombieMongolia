import { useCallback, useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Button, Card, Select, Space, Table, Tag, Typography, message } from 'antd';
import type { TableProps } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { api, Page, ReportRow } from '../api';
import { dateTime } from '../format';

// Чатын гомдол (App Store 1.2, Google Play): тоглогчид тоглоом доторх чатаас өөр тоглогчийн талаар мэдүүлнэ.
// Шалгаад шаардлагатай бол тоглогчийн хуудаснаас дансыг хаана, дараа нь "Шийдвэрлэсэн" болгоно.
export default function ReportsPage() {
  const [data, setData] = useState<(Page<ReportRow> & { open_count: number }) | null>(null);
  const [loading, setLoading] = useState(false);
  const [query, setQuery] = useState({ resolved: '0', page: 1, size: 20 });

  const load = useCallback(() => {
    setLoading(true);
    api<Page<ReportRow> & { open_count: number }>('reports/', { query })
      .then(setData)
      .catch(() => setData(null))
      .finally(() => setLoading(false));
  }, [query]);

  useEffect(load, [load]);

  const resolve = (row: ReportRow, resolved: boolean) =>
    api(`reports/${row.id}/resolve/`, { method: 'POST', body: { resolved } })
      .then(() => {
        message.success(resolved ? 'Шийдвэрлэсэн' : 'Дахин нээлээ');
        load();
      })
      .catch((error) => message.error(String(error?.message ?? error)));

  const person = (p: ReportRow['reported'], fallback: string) =>
    p ? (
      <Space direction="vertical" size={0}>
        <Link to={`/players/${p.id}`}>
          <Typography.Text strong>{p.name}</Typography.Text>
        </Link>
        <Typography.Text type="secondary" className="mono">
          {p.phone}
        </Typography.Text>
        {p.blocked && <Tag color="red">Хаагдсан</Tag>}
      </Space>
    ) : (
      <Typography.Text type="secondary">{fallback || '—'}</Typography.Text>
    );

  const columns: TableProps<ReportRow>['columns'] = [
    { title: 'Огноо', dataIndex: 'created', width: 150, render: (value) => dateTime(value) },
    {
      title: 'Гомдол гаргасан тоглогч',
      dataIndex: 'reported',
      render: (_, row) => (
        <Space direction="vertical" size={0}>
          {person(row.reported, row.reported_name)}
          {row.reported && row.reported.name !== row.reported_name && (
            <Typography.Text type="secondary">тоглоомд: {row.reported_name}</Typography.Text>
          )}
        </Space>
      ),
    },
    {
      title: 'Мессежүүд',
      dataIndex: 'messages',
      render: (lines: string[]) =>
        lines.length ? (
          <Space direction="vertical" size={0}>
            {lines.map((line, index) => (
              <Typography.Text key={index}>{line}</Typography.Text>
            ))}
          </Space>
        ) : (
          <Typography.Text type="secondary">(мессежгүй)</Typography.Text>
        ),
    },
    { title: 'Мэдүүлсэн', dataIndex: 'reporter', render: (_, row) => person(row.reporter, '') },
    {
      title: 'Өрөө',
      dataIndex: 'room_id',
      width: 80,
      render: (value) => (value ? <Link to={`/logs?room=${value}&hours=336`}>{value}</Link> : '—'),
    },
    {
      title: '',
      key: 'actions',
      width: 150,
      render: (_, row) =>
        row.resolved ? (
          <Button size="small" onClick={() => resolve(row, false)}>
            Дахин нээх
          </Button>
        ) : (
          <Button size="small" type="primary" onClick={() => resolve(row, true)}>
            Шийдвэрлэсэн
          </Button>
        ),
    },
  ];

  return (
    <Card
      title={
        <Space>
          Чатын гомдол
          {data && data.open_count > 0 && <Tag color="orange">{data.open_count} шийдвэрлээгүй</Tag>}
        </Space>
      }
      extra={
        <Space>
          <Select
            value={query.resolved}
            style={{ width: 170 }}
            onChange={(resolved) => setQuery({ ...query, resolved, page: 1 })}
            options={[
              { value: '0', label: 'Шийдвэрлээгүй' },
              { value: '1', label: 'Шийдвэрлэсэн' },
              { value: '', label: 'Бүгд' },
            ]}
          />
          <Button icon={<ReloadOutlined />} onClick={load} />
        </Space>
      }
    >
      <Table<ReportRow>
        rowKey="id"
        loading={loading}
        columns={columns}
        dataSource={data?.items ?? []}
        scroll={{ x: 900 }}
        pagination={{
          current: query.page,
          pageSize: query.size,
          total: data?.total ?? 0,
          onChange: (page, size) => setQuery({ ...query, page, size }),
        }}
      />
    </Card>
  );
}
