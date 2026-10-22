import { useState } from 'react';
import { Link } from 'react-router-dom';
import { App, Button, Dropdown, Input, Modal, Space, Table, Tag, Tooltip, Typography } from 'antd';
import type { TablePaginationConfig, TableProps } from 'antd';
import { CheckCircleOutlined, MoreOutlined, SyncOutlined } from '@ant-design/icons';
import { api, InvoiceRow } from '../api';
import { dateTime, KIND_LABEL, money, PLATFORM_LABEL, STATUS_LABEL } from '../format';

export default function InvoiceTable({
  items,
  loading,
  pagination,
  onPage,
  onChanged,
  compact,
}: {
  items?: InvoiceRow[];
  loading?: boolean;
  pagination?: TablePaginationConfig | false;
  onPage?: (page: number, size: number) => void;
  onChanged: () => void;
  compact?: boolean;
}) {
  const { message, modal } = App.useApp();
  const [working, setWorking] = useState<number | null>(null);
  const [markTarget, setMarkTarget] = useState<InvoiceRow | null>(null);
  const [note, setNote] = useState('');

  const check = async (row: InvoiceRow) => {
    setWorking(row.id);
    try {
      const r = await api<{ invoice: InvoiceRow }>(`invoices/${row.id}/check/`, { method: 'POST', body: {} });
      if (r.invoice.status === 'paid') message.success('Төлөгдсөн байна — эрх олгогдлоо');
      else message.info('QPay дээр төлөгдөөгүй байна');
      onChanged();
    } catch (e) {
      message.error((e as Error).message);
    } finally {
      setWorking(null);
    }
  };

  const markPaid = async (row: InvoiceRow, force = false) => {
    setWorking(row.id);
    try {
      await api(`invoices/${row.id}/mark-paid/`, { method: 'POST', body: { note, force } });
      message.success('Төлсөн гэж тэмдэглэж эрх олгогдлоо');
      setMarkTarget(null);
      setNote('');
      onChanged();
    } catch (e) {
      const error = e as Error & { status?: number };
      if (error.status === 409) {
        modal.confirm({
          title: 'Цуцлагдсан нэхэмжлэх',
          content: 'Энэ нэхэмжлэх цуцлагдсан байна. Мөнгө орсныг QPay-ээс баталгаажуулсан бол үргэлжлүүлнэ үү.',
          okText: 'Үргэлжлүүлэх',
          cancelText: 'Болих',
          onOk: () => markPaid(row, true),
        });
      } else {
        message.error(error.message);
      }
    } finally {
      setWorking(null);
    }
  };

  const columns: TableProps<InvoiceRow>['columns'] = [
    { title: '#', dataIndex: 'id', width: 70, render: (v) => <span className="mono">{v}</span> },
    ...(compact
      ? []
      : [
          {
            title: 'Тоглогч',
            dataIndex: 'player',
            render: (p: InvoiceRow['player']) => (
              <Space direction="vertical" size={0}>
                <Link to={`/players/${p.id}`}>{p.name}</Link>
                <Typography.Text type="secondary" className="mono">
                  {p.phone}
                </Typography.Text>
              </Space>
            ),
          },
        ]),
    {
      title: 'Төрөл',
      dataIndex: 'kind',
      render: (kind, row) => (
        <Space direction="vertical" size={0}>
          {KIND_LABEL[kind]}
          <Typography.Text type="secondary" style={{ fontSize: 12 }}>
            {kind === 'pack' ? `${row.credits} эрх` : ''} {PLATFORM_LABEL[row.platform] ?? row.platform}
          </Typography.Text>
        </Space>
      ),
    },
    { title: 'Дүн', dataIndex: 'amount', align: 'right', render: (v) => <b>{money(v)}</b> },
    {
      title: 'Төлөв',
      dataIndex: 'status',
      render: (status, row) => (
        <Space size={4} wrap>
          <Tag color={STATUS_LABEL[status]?.color}>{STATUS_LABEL[status]?.text ?? status}</Tag>
          {status === 'paid' && !row.applied && (
            <Tooltip title="Төлөгдсөн ч эрх олгогдоогүй (төхөөрөмжийн зөрчил)">
              <Tag color="red">Олгоогүй</Tag>
            </Tooltip>
          )}
        </Space>
      ),
    },
    {
      title: 'Огноо',
      dataIndex: 'created',
      responsive: ['md'],
      render: (v, row) => (
        <Space direction="vertical" size={0}>
          <span>{dateTime(v)}</span>
          {row.paid_at && (
            <Typography.Text type="success" style={{ fontSize: 12 }}>
              Төлсөн {dateTime(row.paid_at)}
            </Typography.Text>
          )}
        </Space>
      ),
    },
    {
      title: '',
      key: 'actions',
      width: 56,
      render: (_, row) => (
        <Dropdown
          trigger={['click']}
          menu={{
            items: [
              { key: 'check', icon: <SyncOutlined />, label: 'QPay-ээс шалгах', disabled: row.status !== 'pending' },
              { key: 'mark', icon: <CheckCircleOutlined />, label: 'Төлсөн гэж тэмдэглэх', disabled: row.applied },
              { type: 'divider' },
              { key: 'qpay', label: <span className="mono">QPay: {row.qpay_invoice_id || '—'}</span>, disabled: true },
              { key: 'payment', label: <span className="mono">Гүйлгээ: {row.qpay_payment_id || '—'}</span>, disabled: true },
            ],
            onClick: ({ key }) => {
              if (key === 'check') check(row);
              if (key === 'mark') setMarkTarget(row);
            },
          }}
        >
          <Button type="text" icon={<MoreOutlined />} loading={working === row.id} />
        </Dropdown>
      ),
    },
  ];

  return (
    <>
      <Table<InvoiceRow>
        rowKey="id"
        size={compact ? 'small' : 'middle'}
        loading={loading}
        columns={columns}
        dataSource={items}
        scroll={{ x: 640 }}
        pagination={pagination ?? (compact ? { pageSize: 10 } : false)}
        onChange={(p) => onPage?.(p.current ?? 1, p.pageSize ?? 20)}
      />
      <Modal
        title={`Нэхэмжлэх #${markTarget?.id} — төлсөн гэж тэмдэглэх`}
        open={!!markTarget}
        okText="Эрх олгох"
        okButtonProps={{ danger: true }}
        cancelText="Болих"
        confirmLoading={working === markTarget?.id}
        onCancel={() => setMarkTarget(null)}
        onOk={() => markTarget && markPaid(markTarget)}
      >
        <Typography.Paragraph>
          <b>{markTarget && money(markTarget.amount)}</b> — {markTarget && KIND_LABEL[markTarget.kind]}. Мөнгө орсныг QPay эсвэл банкны хуулгаас
          баталгаажуулсан бол л тэмдэглэнэ үү. Үйлдэл таны нэрээр логт бичигдэнэ.
        </Typography.Paragraph>
        <Input.TextArea rows={2} placeholder="Тайлбар (гүйлгээний дугаар г.м.)" value={note} onChange={(e) => setNote(e.target.value)} />
      </Modal>
    </>
  );
}
