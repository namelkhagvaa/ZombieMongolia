import { useCallback, useEffect, useState } from 'react';
import { App, Button, Card, Input, Modal, Select, Space, Statistic, Table, Tag, Tooltip, Typography } from 'antd';
import type { TableProps } from 'antd';
import { CheckOutlined, CloseOutlined, ReloadOutlined } from '@ant-design/icons';
import { api, ApiError, CashoutRow, Page } from '../api';
import { ago, dateTime } from '../format';

const STATUS: Record<CashoutRow['status'], { label: string; color?: string }> = {
  verifying: { label: 'Утас баталгаажуулж буй', color: 'blue' },
  pending: { label: 'Шилжүүлэх', color: 'gold' },
  paid: { label: 'Олгосон', color: 'green' },
  rejected: { label: 'Татгалзсан', color: 'red' },
  expired: { label: 'Хугацаа дууссан' },
  cancelled: { label: 'Цуцалсан' },
};

const money = (value: number) => value.toLocaleString('mn-MN').replace(/,/g, ' ') + '₮';

type Listing = Page<CashoutRow> & { pending_total: number; pending_count: number; paid_total: number };

// Gold татах хүсэлт: тоглогч утсаа баталгаажуулсан (Шилжүүлэх) хүсэлтийг админ банкаар гараар шилжүүлээд
// "Олгосон" гэж тэмдэглэнэ; татгалзвал Gold тоглогчид буцна.
export default function CashoutsPage() {
  const { message } = App.useApp();
  const [data, setData] = useState<Listing | null>(null);
  const [loading, setLoading] = useState(false);
  const [query, setQuery] = useState({ status: 'pending', q: '', page: 1, size: 20 });
  const [action, setAction] = useState<{ row: CashoutRow; kind: 'paid' | 'reject' } | null>(null);
  const [note, setNote] = useState('');
  const [saving, setSaving] = useState(false);

  const load = useCallback(() => {
    setLoading(true);
    api<Listing>('cashouts/', { query })
      .then(setData)
      .catch(() => setData(null))
      .finally(() => setLoading(false));
  }, [query]);

  useEffect(load, [load]);

  const submit = async () => {
    if (!action) return;
    setSaving(true);
    try {
      await api(`cashouts/${action.row.id}/${action.kind}/`, { method: 'POST', body: { note } });
      message.success(action.kind === 'paid' ? 'Олгосон гэж тэмдэглэлээ' : 'Татгалзлаа — Gold тоглогчид буцлаа');
      setAction(null);
      load();
    } catch (e) {
      message.error((e as ApiError).message);
    } finally {
      setSaving(false);
    }
  };

  const columns: TableProps<CashoutRow>['columns'] = [
    { title: '#', dataIndex: 'id', width: 70 },
    {
      title: 'Тоглогч',
      dataIndex: 'player',
      render: (_, row) => (
        <Space direction="vertical" size={0}>
          <Typography.Text strong>{row.player.name}</Typography.Text>
          <Typography.Text type="secondary" className="mono">
            {row.player.phone}
          </Typography.Text>
        </Space>
      ),
    },
    {
      title: 'Дүн',
      dataIndex: 'amount',
      render: (value, row) => (
        <Space direction="vertical" size={0}>
          <Typography.Text strong>{money(value)}</Typography.Text>
          <Typography.Text type="secondary">
            {row.gold.toLocaleString()} Gold · 1 Gold = {row.rate}₮
          </Typography.Text>
        </Space>
      ),
    },
    {
      title: 'Данс',
      dataIndex: 'account',
      render: (_, row) => (
        <Space direction="vertical" size={0}>
          <Typography.Text>{row.bank}</Typography.Text>
          <Typography.Text copyable className="mono">
            {row.account}
          </Typography.Text>
          <Typography.Text type="secondary">{row.holder}</Typography.Text>
        </Space>
      ),
    },
    {
      title: 'Төлөв',
      dataIndex: 'status',
      render: (value: CashoutRow['status'], row) => (
        <Space direction="vertical" size={0}>
          <Tag color={STATUS[value].color}>{STATUS[value].label}</Tag>
          {row.note && <Typography.Text type="secondary">{row.note}</Typography.Text>}
          {row.handled_by && <Typography.Text type="secondary">{row.handled_by}</Typography.Text>}
        </Space>
      ),
    },
    {
      title: 'Хүссэн',
      dataIndex: 'created',
      width: 140,
      render: (value, row) => (
        <Tooltip title={`${dateTime(value)}${row.closed_at ? ' — ' + dateTime(row.closed_at) : ''}`}>
          <span>{ago(value)}</span>
        </Tooltip>
      ),
    },
    {
      title: '',
      key: 'actions',
      width: 200,
      render: (_, row) =>
        row.status === 'pending' || row.status === 'verifying' ? (
          <Space>
            {row.status === 'pending' && (
              <Button type="primary" size="small" icon={<CheckOutlined />} onClick={() => { setNote(''); setAction({ row, kind: 'paid' }); }}>
                Олгосон
              </Button>
            )}
            <Button danger size="small" icon={<CloseOutlined />} onClick={() => { setNote(''); setAction({ row, kind: 'reject' }); }}>
              Татгалзах
            </Button>
          </Space>
        ) : null,
    },
  ];

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Gold татах</h1>
          <p>Тоглоомоос хожсон Gold-оо банкны данс руу татах хүсэлтүүд</p>
        </div>
        <Space wrap>
          <Input.Search
            placeholder="Нэр, утас, данс"
            allowClear
            style={{ width: 200 }}
            onSearch={(q) => setQuery({ ...query, q, page: 1 })}
          />
          <Select
            value={query.status}
            style={{ width: 200 }}
            onChange={(status) => setQuery({ ...query, status, page: 1 })}
            options={[
              { value: '', label: 'Бүгд' },
              ...Object.entries(STATUS).map(([value, item]) => ({ value, label: item.label })),
            ]}
          />
          <Button icon={<ReloadOutlined />} onClick={load} loading={loading}>
            Шинэчлэх
          </Button>
        </Space>
      </div>
      <Space size={16} wrap style={{ marginBottom: 16 }}>
        <Card variant="borderless">
          <Statistic title="Шилжүүлэх хүсэлт" value={data?.pending_count ?? 0} />
        </Card>
        <Card variant="borderless">
          <Statistic title="Шилжүүлэх дүн" value={money(data?.pending_total ?? 0)} />
        </Card>
        <Card variant="borderless">
          <Statistic title="Нийт олгосон" value={money(data?.paid_total ?? 0)} />
        </Card>
      </Space>
      <Card variant="borderless">
        <Table
          rowKey="id"
          columns={columns}
          dataSource={data?.items ?? []}
          loading={loading}
          scroll={{ x: 900 }}
          pagination={{
            current: query.page,
            pageSize: query.size,
            total: data?.total ?? 0,
            onChange: (page, size) => setQuery({ ...query, page, size }),
          }}
        />
      </Card>
      <Modal
        open={!!action}
        title={action?.kind === 'paid' ? 'Олгосон гэж тэмдэглэх' : 'Хүсэлтийг татгалзах'}
        okText={action?.kind === 'paid' ? 'Олгосон' : 'Татгалзах'}
        okButtonProps={{ danger: action?.kind === 'reject', loading: saving, disabled: action?.kind === 'reject' && !note.trim() }}
        cancelText="Болих"
        onOk={submit}
        onCancel={() => setAction(null)}
      >
        {action && (
          <Space direction="vertical" style={{ width: '100%' }}>
            <Typography.Text>
              {action.row.player.name}: <b>{money(action.row.amount)}</b> → {action.row.bank} <span className="mono">{action.row.account}</span> ({action.row.holder})
            </Typography.Text>
            <Typography.Text type="secondary">
              {action.kind === 'paid'
                ? 'Банкаар шилжүүлсний дараа л тэмдэглэнэ. Гүйлгээний дугаараа бичиж болно.'
                : 'Gold тоглогчид буцна. Шалтгаан тоглогчид харагдана.'}
            </Typography.Text>
            <Input.TextArea
              rows={2}
              maxLength={200}
              value={note}
              placeholder={action.kind === 'paid' ? 'Гүйлгээний дугаар (заавал биш)' : 'Шалтгаан (жишээ нь: дансны дугаар буруу)'}
              onChange={(e) => setNote(e.target.value)}
            />
          </Space>
        )}
      </Modal>
    </>
  );
}
