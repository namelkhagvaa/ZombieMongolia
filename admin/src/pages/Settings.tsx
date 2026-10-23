import { useEffect, useState } from 'react';
import { Alert, App, Button, Card, Col, Form, InputNumber, Row, Skeleton, Typography } from 'antd';
import { SaveOutlined } from '@ant-design/icons';
import { api, Settings } from '../api';
import { useAuth } from '../auth';

export default function SettingsPage() {
  const { message } = App.useApp();
  const { user } = useAuth();
  const [settings, setSettings] = useState<Settings | null>(null);
  const [saving, setSaving] = useState(false);
  const [form] = Form.useForm<Settings>();
  const values = Form.useWatch([], form);
  const editable = !!user?.superuser;

  useEffect(() => {
    api<{ settings: Settings }>('settings/')
      .then((r) => {
        setSettings(r.settings);
        form.setFieldsValue(r.settings);
      })
      .catch((e) => message.error(e.message));
  }, [form, message]);

  const save = async (v: Settings) => {
    setSaving(true);
    try {
      const r = await api<{ settings: Settings }>('settings/', {
        method: 'PUT',
        body: {
          device_change_price: v.device_change_price,
          max_devices: v.max_devices,
          start_gold: v.start_gold,
          price_damage: v.price_damage,
          price_fire_rate: v.price_fire_rate,
          price_magazine: v.price_magazine,
          price_health: v.price_health,
          price_soldier: v.price_soldier,
        },
      });
      setSettings(r.settings);
      form.setFieldsValue(r.settings);
      message.success('Хадгаллаа. Тоглоом дараагийн удаа шалгахад шинэ үнэ хэрэглэгдэнэ.');
    } catch (e) {
      message.error((e as Error).message);
    } finally {
      setSaving(false);
    }
  };

  const upgradePrices: { name: keyof Settings; label: string; max: number }[] = [
    { name: 'price_damage', label: 'Хохирол', max: 5 },
    { name: 'price_fire_rate', label: 'Буудах хурд', max: 5 },
    { name: 'price_magazine', label: 'Сумны хайрцаг', max: 5 },
    { name: 'price_health', label: 'Амь', max: 5 },
    { name: 'price_soldier', label: 'Цэрэг', max: 4 },
  ];

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Тохиргоо</h1>
          <p>Gold, дэлгүүрийн үнэ, төхөөрөмж — тоглоом серверээс шууд уншина. Бүх аймаг үнэгүй.</p>
        </div>
      </div>
      {!editable && <Alert type="info" showIcon message="Зөвхөн ерөнхий админ (superuser) өөрчилнө." style={{ marginBottom: 16 }} />}
      {!settings ? (
        <Skeleton active />
      ) : (
        <Form form={form} layout="vertical" onFinish={save} disabled={!editable} requiredMark={false}>
          <Row gutter={[16, 16]}>
            <Col xs={24} lg={12}>
              <Card title="Gold ба дэлгүүр" variant="borderless" style={{ height: '100%' }}>
                <Form.Item
                  name="start_gold"
                  label="Шинэ тоглогчийн Gold"
                  extra="Бүртгүүлэхэд нэг удаа олгоно."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={100000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                {upgradePrices.map((item) => (
                  <Form.Item
                    key={item.name}
                    name={item.name}
                    label={`${item.label} (1–${item.max} түвшин)`}
                    extra={values?.[item.name] ? `Дараагийн түвшин = ${values[item.name]} × түвшин (бүгд: ${
                      (values[item.name] as number) * ((item.max * (item.max + 1)) / 2)
                    } Gold)` : undefined}
                    rules={[{ required: true }]}
                  >
                    <InputNumber min={0} max={100000} style={{ width: 200 }} suffix="Gold" />
                  </Form.Item>
                ))}
              </Card>
            </Col>
            <Col xs={24} lg={12}>
              <Card title="Төхөөрөмж" variant="borderless" style={{ height: '100%' }}>
                <Form.Item
                  name="max_devices"
                  label="Нэг дансны төхөөрөмж"
                  extra="Ийм тооны төхөөрөмж үнэгүй бүртгэнэ. Нэг зэрэг зөвхөн нэг төхөөрөмж дээр нэвтэрнэ."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={1} max={5} style={{ width: 200 }} suffix="төхөөрөмж" />
                </Form.Item>
                <Form.Item
                  name="device_change_price"
                  label="Төхөөрөмж солих үнэ"
                  extra="Байр дүүрсэн үед шинэ төхөөрөмж нэмэхэд төлнө (хамгийн эрт бүртгэсэн нь чөлөөлөгдөнө)."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={1000000} step={500} style={{ width: 200 }} suffix="₮" />
                </Form.Item>
                <Typography.Paragraph type="secondary">Аймгийн тоо: {settings.province_count} (бүгд үнэгүй)</Typography.Paragraph>
              </Card>
            </Col>
          </Row>
          {editable && (
            <Button type="primary" htmlType="submit" icon={<SaveOutlined />} loading={saving} size="large" style={{ marginTop: 16 }}>
              Хадгалах
            </Button>
          )}
        </Form>
      )}
    </>
  );
}
