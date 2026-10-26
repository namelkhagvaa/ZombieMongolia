import { useEffect, useState } from 'react';
import { Alert, App, Button, Card, Col, Form, InputNumber, Row, Skeleton, Space, Switch, Typography } from 'antd';
import { DeleteOutlined, PlusOutlined, SaveOutlined } from '@ant-design/icons';
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
          skin_price: v.skin_price,
          skin_price_iron: v.skin_price_iron,
          coop_reward: v.coop_reward,
          coop_daily_limit: v.coop_daily_limit,
          room_fee: v.room_fee,
          room_stake: v.room_stake,
          stage_bonus: v.stage_bonus,
          stage_min_minutes: v.stage_min_minutes,
          battle_pair_limit: v.battle_pair_limit,
          stage_daily_limit: v.stage_daily_limit,
          verify_bonus: v.verify_bonus,
          register_per_ip_day: v.register_per_ip_day,
          block_same_ip: v.block_same_ip,
          gold_packs: v.gold_packs,
          cashout_enabled: v.cashout_enabled,
          cashout_min: v.cashout_min,
          cashout_rate: v.cashout_rate,
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

  // flat: нэг бүр ижил үнэтэй (цэрэг: тоо, тоглолтод алдвал хасагдана)
  const upgradePrices: { name: keyof Settings; label: string; max: number; flat?: boolean }[] = [
    { name: 'price_damage', label: 'Хохирол', max: 5 },
    { name: 'price_fire_rate', label: 'Буудах хурд', max: 5 },
    { name: 'price_magazine', label: 'Сумны хайрцаг', max: 5 },
    { name: 'price_health', label: 'Амь', max: 5 },
    { name: 'price_soldier', label: 'Цэрэг', max: 10, flat: true },
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
                    label={item.flat ? `${item.label} (нэг бүр, дээд тал ${item.max})` : `${item.label} (1–${item.max} түвшин)`}
                    extra={!values?.[item.name] ? undefined : item.flat
                      ? `Нэг цэрэг = ${values[item.name]} Gold. Тоглолтод үхсэн цэрэг шууд хасагдана.`
                      : `Дараагийн түвшин = ${values[item.name]} × түвшин (бүгд: ${
                          (values[item.name] as number) * ((item.max * (item.max + 1)) / 2)
                        } Gold)`}
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
            <Col xs={24} lg={12}>
              <Card title="Нээлттэй өрөө ба тулаан" variant="borderless" style={{ height: '100%' }}>
                <Form.Item
                  name="room_fee"
                  label="Өрөөнд орох төлбөр"
                  extra="Хост ба зочин хоёулаа төлнө (ганцаарчилсан тоглолтод үгүй)."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={10000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="room_stake"
                  label="Барьцаа (ялагдвал өгөх)"
                  extra={`Орохын тулд нийт ${(values?.room_fee ?? 0) + (values?.room_stake ?? 0)} Gold хэрэгтэй; бага бол ганцаарчилсан.`}
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={10000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="skin_price"
                  label="DEMON дүрээр тоглох"
                  extra="Төлбөртэй дүрээр аймаг эхлүүлэх бүрд хасагдана. 0 бол үнэгүй."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={100000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="skin_price_iron"
                  label="IRON дүрээр тоглох"
                  extra="Төлбөртэй дүрээр аймаг эхлүүлэх бүрд хасагдана. 0 бол үнэгүй."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={100000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="coop_reward"
                  label="Найзтайгаа кодоор хамт давсны шагнал"
                  extra="Үеийг хамт давахад хоёуланд нь (хүн бүрд) урамшууллын Gold — татах боломжгүй. 0 бол шагналгүй."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={10000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="coop_daily_limit"
                  label="Хамт давсны шагнал өдөрт"
                  extra="Нэг тоглогч өдөрт ийм удаа хүртэл хамт давсны шагнал авна."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={100} style={{ width: 200 }} suffix="удаа" />
                </Form.Item>
                <Form.Item name="stage_bonus" label="Үе шат дуусгасны шагнал" rules={[{ required: true }]}>
                  <InputNumber min={0} max={10000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="stage_min_minutes"
                  label="Шагнал авах хамгийн бага хугацаа"
                  extra="Өрөө нээгдсэнээс хойш ийм хугацаанаас өмнө дуусгавал шагнал олгохгүй."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={240} style={{ width: 200 }} suffix="минут" />
                </Form.Item>
                <Form.Item
                  name="battle_pair_limit"
                  label="Нэг хосын хоногийн төлбөртэй тулаан"
                  extra="Ижил хоёр тоглогч хоногт үүнээс олон удаа тулалдвал Gold шилжихгүй (зориуд ээлжлэн ялагдах)."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={100} style={{ width: 200 }} suffix="тулаан" />
                </Form.Item>
                <Form.Item
                  name="verify_bonus"
                  label="Утас баталгаажуулсны урамшуулал"
                  extra="Тоглогч утсаа баталгаажуулахад нэг удаа олгоно. Баталгаажаагүй тоглогчид шагнал, тулааны хожил нэмэгдэхгүй."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={100000} step={50} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="register_per_ip_day"
                  label="Нэг IP-ээс хоногт нээх данс"
                  extra="Олон данс нээж эхлэлийн Gold цуглуулахаас. Утасны операторууд олон хүнийг нэг IP-аар гаргадаг тул хэт бага бүү тавиарай."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={1} max={1000} style={{ width: 200 }} suffix="данс" />
                </Form.Item>
                <Form.Item
                  name="stage_daily_limit"
                  label="Үе шатны шагнал хоногт"
                  extra="Нэг тоглогч хоногт үүнээс олон удаа үе шатны шагнал авахгүй (олборлохоос)."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0} max={100} style={{ width: 200 }} suffix="удаа" />
                </Form.Item>
                <Form.Item
                  name="block_same_ip"
                  label="Ижил IP-тай тоглогчдын тулаанд Gold шилжүүлэхгүй"
                  extra="Нэг гэрийн роутер (ижил IP) дээрх тоглогчид тулалдаж болно, гэхдээ барьцаа ялагчид очихгүй: нэг хүн олон дансаар Gold цуглуулахаас сэргийлнэ."
                  valuePropName="checked"
                >
                  <Switch />
                </Form.Item>
              </Card>
            </Col>
            <Col xs={24} lg={12}>
              <Card title="Gold худалдах багц (вэб: /api/topup/)" variant="borderless" style={{ height: '100%' }}>
                <Form.List name="gold_packs">
                  {(fields, { add, remove }) => (
                    <>
                      {fields.map((field) => (
                        <Space key={field.key} align="baseline" wrap>
                          <Form.Item name={[field.name, 'gold']} rules={[{ required: true }]}>
                            <InputNumber min={1} max={1000000} style={{ width: 150 }} suffix="Gold" />
                          </Form.Item>
                          <Form.Item name={[field.name, 'price']} rules={[{ required: true }]}>
                            <InputNumber min={100} max={10000000} step={500} style={{ width: 170 }} suffix="₮" />
                          </Form.Item>
                          {fields.length > 1 && <Button icon={<DeleteOutlined />} onClick={() => remove(field.name)} />}
                        </Space>
                      ))}
                      {fields.length < 8 && (
                        <Button icon={<PlusOutlined />} onClick={() => add({ gold: 100, price: 1000 })}>
                          Багц нэмэх
                        </Button>
                      )}
                    </>
                  )}
                </Form.List>
              </Card>
            </Col>
            <Col xs={24} lg={12}>
              <Card title="Gold татах (зөвхөн тоглоомоос хожсон Gold)" variant="borderless" style={{ height: '100%' }}>
                <Form.Item name="cashout_enabled" label="Gold татах нээлттэй" valuePropName="checked">
                  <Switch />
                </Form.Item>
                <Form.Item
                  name="cashout_min"
                  label="Татах доод хэмжээ"
                  extra="Тулаан, үе шатаас хожсон Gold-оос л татна; цэнэглэсэн Gold татагдахгүй."
                  rules={[{ required: true }]}
                >
                  <InputNumber min={1} max={10000000} style={{ width: 200 }} suffix="Gold" />
                </Form.Item>
                <Form.Item
                  name="cashout_rate"
                  label="Ханш: 1 Gold ="
                  extra={values?.cashout_min && values?.cashout_rate
                    ? `${values.cashout_min} Gold = ${Math.floor(values.cashout_min * values.cashout_rate)}₮`
                    : undefined}
                  rules={[{ required: true }]}
                >
                  <InputNumber min={0.01} max={1000} step={0.1} style={{ width: 200 }} suffix="₮" />
                </Form.Item>
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
