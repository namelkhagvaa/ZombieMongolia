import { useState } from 'react';
import { Alert, Button, Card, ConfigProvider, Form, Input, Typography, theme } from 'antd';
import { LockOutlined, UserOutlined } from '@ant-design/icons';
import { api, setToken, StaffUser } from '../api';

export default function LoginPage({ onLogin }: { onLogin: (user: StaffUser) => void }) {
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const submit = async (values: { username: string; password: string }) => {
    setLoading(true);
    setError('');
    try {
      const result = await api<{ token: string; user: StaffUser }>('login/', { method: 'POST', body: values });
      setToken(result.token);
      onLogin(result.user);
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <ConfigProvider theme={{ algorithm: theme.darkAlgorithm, token: { colorBgContainer: '#141922' } }}>
      <div className="login-wrap">
        <Card className="login-card" variant="borderless" style={{ boxShadow: '0 30px 80px rgba(0,0,0,.45)' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 24 }}>
            <div className="brand-logo" style={{ width: 44, height: 44, borderRadius: 12 }} />
            <div>
              <Typography.Title level={4} style={{ margin: 0 }}>
                ZOMBIE <span style={{ color: '#f0b030' }}>MONGOLIA</span>
              </Typography.Title>
              <Typography.Text type="secondary">Удирдлагын самбар</Typography.Text>
            </div>
          </div>
          {error && <Alert type="error" message={error} showIcon style={{ marginBottom: 16 }} />}
          <Form layout="vertical" onFinish={submit} requiredMark={false} size="large">
            <Form.Item name="username" label="Нэвтрэх нэр" rules={[{ required: true, message: 'Нэрээ оруулна уу' }]}>
              <Input prefix={<UserOutlined />} autoFocus autoComplete="username" />
            </Form.Item>
            <Form.Item name="password" label="Нууц үг" rules={[{ required: true, message: 'Нууц үгээ оруулна уу' }]}>
              <Input.Password prefix={<LockOutlined />} autoComplete="current-password" />
            </Form.Item>
            <Button type="primary" htmlType="submit" block loading={loading} style={{ color: '#1a1406', fontWeight: 600 }}>
              Нэвтрэх
            </Button>
          </Form>
        </Card>
      </div>
    </ConfigProvider>
  );
}
