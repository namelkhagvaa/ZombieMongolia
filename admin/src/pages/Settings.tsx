import { useEffect, useState } from 'react';
import { Alert, App, Button, Card, Col, Form, InputNumber, Row, Skeleton, Typography } from 'antd';
import { SaveOutlined } from '@ant-design/icons';
import { api, Settings } from '../api';
import { useAuth } from '../auth';
import { money, provinceName } from '../format';

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
          free_provinces: v.free_provinces,
          pack_size: v.pack_size,
          pack_price: v.pack_price,
          device_change_price: v.device_change_price,
          max_devices: v.max_devices,
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

  const perProvince = values?.pack_price && values?.pack_size ? Math.round(values.pack_price / values.pack_size) : 0;

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Тохиргоо</h1>
          <p>Үнэ, үнэгүй эрх — тоглоом серверээс шууд уншина</p>
        </div>
      </div>
      {!editable && <Alert type="info" showIcon message="Зөвхөн ерөнхий админ (superuser) өөрчилнө." style={{ marginBottom: 16 }} />}
      {!settings ? (
        <Skeleton active />
      ) : (
        <Form form={form} layout="vertical" onFinish={save} disabled={!editable} requiredMark={false}>
          <Row gutter={[16, 16]}>
            <Col xs={24} lg={12}>
              <Card title="Аймгийн эрх" variant="borderless" style={{ height: '100%' }}>
                <Form.Item
                  name="free_provinces"
                  label="Үнэгүй аймгийн тоо"
                  extra={`Бүртгүүлсэн тоглогч эхний ${values?.free_provinces ?? '…'} аймгаа үнэгүй сонгоно (${provinceName(settings.capital_index)} орохгүй).`}
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={22} style={{ width: 200 }} suffix="аймаг" />
                </Form.Item>
                <Form.Item name="pack_size" label="Багц дахь эрх" rules={[{ required: true }]}>
                  <InputNumber min={1} max={22} style={{ width: 200 }} suffix="аймаг" />
                </Form.Item>
                <Form.Item
                  name="pack_price"
                  label="Багцын үнэ"
                  extra={perProvince ? `Нэг аймаг ≈ ${money(perProvince)}` : undefined}
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={1000000} step={500} style={{ width: 200 }} suffix="₮" />
                </Form.Item>
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
                <Typography.Paragraph type="secondary">
                  Аймгийн тоо: {settings.province_count} · Нийслэл: {provinceName(settings.capital_index)} (үргэлж төлбөртэй)
                </Typography.Paragraph>
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
