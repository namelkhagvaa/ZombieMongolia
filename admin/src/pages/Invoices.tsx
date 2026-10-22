import { useCallback, useEffect, useState } from 'react';
import { Button, Card, Col, Input, Row, Select, Space, Statistic } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { api, InvoiceRow, Page } from '../api';
import { money } from '../format';
import InvoiceTable from '../components/InvoiceTable';

type InvoicePage = Page<InvoiceRow> & { paid_total: number; paid_count: number };

export default function InvoicesPage() {
  const [data, setData] = useState<InvoicePage | null>(null);
  const [loading, setLoading] = useState(false);
  const [query, setQuery] = useState({ status: '', kind: '', q: '', page: 1, size: 20 });

  const load = useCallback(() => {
    setLoading(true);
    api<InvoicePage>('invoices/', { query })
      .then(setData)
      .catch(() => setData(null))
      .finally(() => setLoading(false));
  }, [query]);

  useEffect(load, [load]);

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Төлбөр</h1>
          <p>QPay нэхэмжлэхүүд: аймгийн багц, төхөөрөмж солих</p>
        </div>
        <Button icon={<ReloadOutlined />} onClick={load} loading={loading}>
          Шинэчлэх
        </Button>
      </div>
      <Row gutter={[16, 16]} style={{ marginBottom: 16 }}>
        <Col xs={12} md={6}>
          <Card variant="borderless">
            <Statistic title="Төлөгдсөн дүн (шүүлтээр)" value={money(data?.paid_total)} valueStyle={{ color: '#f0b030' }} />
          </Card>
        </Col>
        <Col xs={12} md={6}>
          <Card variant="borderless">
            <Statistic title="Төлөгдсөн нэхэмжлэх" value={data?.paid_count ?? 0} />
          </Card>
        </Col>
      </Row>
      <Card variant="borderless">
        <Space wrap style={{ marginBottom: 16 }}>
          <Input.Search
            allowClear
            placeholder="Тоглогч, утас, QPay дугаар"
            style={{ width: 280 }}
            onSearch={(q) => setQuery({ ...query, q, page: 1 })}
          />
          <Select
            value={query.status}
            style={{ width: 170 }}
            onChange={(status) => setQuery({ ...query, status, page: 1 })}
            options={[
              { value: '', label: 'Бүх төлөв' },
              { value: 'paid', label: 'Төлөгдсөн' },
              { value: 'pending', label: 'Хүлээгдэж буй' },
              { value: 'cancelled', label: 'Цуцлагдсан' },
            ]}
          />
          <Select
            value={query.kind}
            style={{ width: 180 }}
            onChange={(kind) => setQuery({ ...query, kind, page: 1 })}
            options={[
              { value: '', label: 'Бүх төрөл' },
              { value: 'pack', label: 'Аймгийн багц' },
              { value: 'device', label: 'Төхөөрөмж солих' },
            ]}
          />
        </Space>
        <InvoiceTable
          items={data?.items}
          loading={loading}
          onChanged={load}
          pagination={{ current: query.page, pageSize: query.size, total: data?.total, showSizeChanger: true, showTotal: (n) => `Нийт ${n}` }}
          onPage={(page, size) => setQuery({ ...query, page, size })}
        />
      </Card>
    </>
  );
}
