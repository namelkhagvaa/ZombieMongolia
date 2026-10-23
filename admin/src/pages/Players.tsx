import { useCallback, useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { Button, Card, Input, Select, Space, Table, Tag, Tooltip, Typography } from 'antd';
import type { TableProps } from 'antd';
import { ReloadOutlined, SearchOutlined } from '@ant-design/icons';
import { api, Page, PlayerRow } from '../api';
import { ago, dateTime, money } from '../format';
import PlayerDrawer from '../components/PlayerDrawer';

export default function PlayersPage() {
  const { id } = useParams();
  const navigate = useNavigate();
  const [data, setData] = useState<Page<PlayerRow> | null>(null);
  const [loading, setLoading] = useState(false);
  const [query, setQuery] = useState({ q: '', status: '', sort: '-created', page: 1, size: 20 });

  const load = useCallback(() => {
    setLoading(true);
    api<Page<PlayerRow>>('players/', { query })
      .then(setData)
      .catch(() => setData(null))
      .finally(() => setLoading(false));
  }, [query]);

  useEffect(load, [load]);

  const columns: TableProps<PlayerRow>['columns'] = [
    {
      title: 'Тоглогч',
      dataIndex: 'name',
      sorter: true,
      render: (_, row) => (
        <Space direction="vertical" size={0}>
          <Typography.Text strong>
            {row.name} {row.blocked && <Tag color="red">Хаагдсан</Tag>}
          </Typography.Text>
          <Typography.Text type="secondary" className="mono">
            {row.phone}
          </Typography.Text>
        </Space>
      ),
    },
    {
      title: 'Gold',
      dataIndex: 'gold',
      align: 'right',
      width: 100,
      sorter: true,
      render: (value) => <b style={{ color: '#f0b030' }}>{value.toLocaleString()}</b>,
    },
    {
      title: 'Төлсөн',
      dataIndex: 'paid_total',
      align: 'right',
      width: 120,
      sorter: true,
      render: (value) => (value ? <b style={{ color: '#f0b030' }}>{money(value)}</b> : <Typography.Text type="secondary">—</Typography.Text>),
    },
    {
      title: 'Бүртгүүлсэн',
      dataIndex: 'created',
      width: 170,
      sorter: true,
      responsive: ['lg'],
      render: (value) => (
        <Tooltip title={dateTime(value)}>
          <span>{ago(value)}</span>
        </Tooltip>
      ),
    },
  ];

  const sortKey: Record<string, string> = { name: 'name', gold: 'gold', paid_total: 'paid', created: 'created' };

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Тоглогчид</h1>
          <p>{data ? `${data.total} тоглогч` : 'Бүртгэлтэй тоглогчид'}</p>
        </div>
        <Button icon={<ReloadOutlined />} onClick={load} loading={loading}>
          Шинэчлэх
        </Button>
      </div>
      <Card variant="borderless">
        <Space wrap style={{ marginBottom: 16 }}>
          <Input.Search
            allowClear
            placeholder="Нэр, утасны дугаар"
            prefix={<SearchOutlined />}
            style={{ width: 280 }}
            onSearch={(q) => setQuery({ ...query, q, page: 1 })}
          />
          <Select
            value={query.status}
            style={{ width: 170 }}
            onChange={(status) => setQuery({ ...query, status, page: 1 })}
            options={[
              { value: '', label: 'Бүх тоглогч' },
              { value: 'paying', label: 'Төлбөр төлсөн' },
              { value: 'blocked', label: 'Хаагдсан' },
            ]}
          />
        </Space>
        <Table<PlayerRow>
          rowKey="id"
          loading={loading}
          columns={columns}
          dataSource={data?.items}
          scroll={{ x: 640 }}
          onRow={(row) => ({ onClick: () => navigate(`/players/${row.id}`), style: { cursor: 'pointer' } })}
          onChange={(pagination, _filters, sorter) => {
            const s = Array.isArray(sorter) ? sorter[0] : sorter;
            const field = s?.order ? sortKey[String(s.field)] : '';
            setQuery({
              ...query,
              page: pagination.current ?? 1,
              size: pagination.pageSize ?? 20,
              sort: field ? (s.order === 'descend' ? '-' : '') + field : '-created',
            });
          }}
          pagination={{ current: query.page, pageSize: query.size, total: data?.total, showSizeChanger: true, showTotal: (n) => `Нийт ${n}` }}
        />
      </Card>
      <PlayerDrawer
        playerId={id ? Number(id) : null}
        onClose={() => navigate('/players')}
        onChanged={load}
      />
    </>
  );
}
