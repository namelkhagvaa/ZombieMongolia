import { useEffect, useMemo, useState } from 'react';
import { Navigate, Route, Routes, useLocation, useNavigate } from 'react-router-dom';
import { App as AntApp, Avatar, Button, ConfigProvider, Dropdown, Grid, Layout, Menu, Spin, Tooltip, theme } from 'antd';
import mnMN from 'antd/locale/mn_MN';
import {
  DashboardOutlined,
  FileTextOutlined,
  FireOutlined,
  LogoutOutlined,
  MenuFoldOutlined,
  MenuUnfoldOutlined,
  MoonOutlined,
  SettingOutlined,
  SunOutlined,
  TeamOutlined,
  WalletOutlined,
} from '@ant-design/icons';
import { api, getToken, setToken, setUnauthorizedHandler, StaffUser } from './api';
import { AuthContext } from './auth';
import LoginPage from './pages/Login';
import DashboardPage from './pages/Dashboard';
import PlayersPage from './pages/Players';
import InvoicesPage from './pages/Invoices';
import EventsPage from './pages/Events';
import SettingsPage from './pages/Settings';
import RoomsPage from './pages/Rooms';

const { Sider, Header, Content } = Layout;
const GOLD = '#f0b030';

function loadDark(): boolean {
  try {
    const saved = localStorage.getItem('zm_admin_dark');
    if (saved) return saved === '1';
  } catch {
    /* */
  }
  return window.matchMedia?.('(prefers-color-scheme: dark)').matches ?? true;
}

const MENU = [
  { key: '/', icon: <DashboardOutlined />, label: 'Хянах самбар' },
  { key: '/players', icon: <TeamOutlined />, label: 'Тоглогчид' },
  { key: '/invoices', icon: <WalletOutlined />, label: 'Төлбөр' },
  { key: '/rooms', icon: <FireOutlined />, label: 'Өрөө, тулаан' },
  { key: '/events', icon: <FileTextOutlined />, label: 'Үйлдлийн лог' },
  { key: '/settings', icon: <SettingOutlined />, label: 'Тохиргоо' },
];

function Shell({ user, onLogout, dark, setDark }: { user: StaffUser; onLogout: () => void; dark: boolean; setDark: (v: boolean) => void }) {
  const screens = Grid.useBreakpoint();
  // Эхний кадрт breakpoint хоосон байдаг: цонхны өргөнөөр тооцно
  const mobile = screens.lg === undefined ? window.innerWidth < 992 : !screens.lg;
  const [collapsed, setCollapsed] = useState(mobile);
  const location = useLocation();
  const navigate = useNavigate();
  const { token } = theme.useToken();
  const selected = '/' + (location.pathname.split('/')[1] || '');

  useEffect(() => setCollapsed(mobile), [mobile]);

  return (
    <Layout style={{ minHeight: '100vh' }}>
      <Sider
        theme="dark"
        width={232}
        collapsedWidth={mobile ? 0 : 72}
        collapsible
        trigger={null}
        collapsed={collapsed}
        style={{ position: mobile ? 'fixed' : 'sticky', top: 0, height: '100vh', zIndex: 20, background: '#0f1218' }}
      >
        <div className="brand">
          <div className="brand-logo" />
          {!collapsed && (
            <div>
              ZOMBIE <span style={{ color: GOLD }}>MONGOLIA</span>
              <small>Удирдлагын самбар</small>
            </div>
          )}
        </div>
        <Menu
          theme="dark"
          mode="inline"
          selectedKeys={[selected]}
          items={MENU}
          style={{ background: 'transparent', borderInlineEnd: 0 }}
          onClick={({ key }) => {
            navigate(key);
            if (mobile) setCollapsed(true);
          }}
        />
      </Sider>
      <Layout>
        <Header
          style={{
            position: 'sticky',
            top: 0,
            zIndex: 10,
            display: 'flex',
            alignItems: 'center',
            gap: 8,
            padding: '0 16px',
            background: token.colorBgContainer,
            borderBottom: `1px solid ${token.colorBorderSecondary}`,
          }}
        >
          <Button type="text" icon={collapsed ? <MenuUnfoldOutlined /> : <MenuFoldOutlined />} onClick={() => setCollapsed(!collapsed)} />
          <div style={{ flex: 1 }} />
          <Tooltip title={dark ? 'Цайвар' : 'Бараан'}>
            <Button type="text" icon={dark ? <SunOutlined /> : <MoonOutlined />} onClick={() => setDark(!dark)} />
          </Tooltip>
          <Dropdown
            menu={{
              items: [
                { key: 'role', label: user.superuser ? 'Ерөнхий админ' : 'Ажилтан', disabled: true },
                { type: 'divider' },
                { key: 'logout', icon: <LogoutOutlined />, label: 'Гарах', danger: true },
              ],
              onClick: ({ key }) => key === 'logout' && onLogout(),
            }}
          >
            <Button type="text" style={{ display: 'flex', alignItems: 'center', gap: 8, height: 40 }}>
              <Avatar size={28} style={{ background: GOLD, color: '#1a1406', fontWeight: 700 }}>
                {user.username.slice(0, 1).toUpperCase()}
              </Avatar>
              {!mobile && user.username}
            </Button>
          </Dropdown>
        </Header>
        <Content style={{ padding: mobile ? 16 : 24, maxWidth: 1480, width: '100%', margin: '0 auto' }}>
          <Routes>
            <Route path="/" element={<DashboardPage />} />
            <Route path="/players" element={<PlayersPage />} />
            <Route path="/players/:id" element={<PlayersPage />} />
            <Route path="/invoices" element={<InvoicesPage />} />
            <Route path="/events" element={<EventsPage />} />
            <Route path="/rooms" element={<RoomsPage />} />
            <Route path="/settings" element={<SettingsPage />} />
            <Route path="*" element={<Navigate to="/" replace />} />
          </Routes>
        </Content>
      </Layout>
      {mobile && !collapsed && (
        <div onClick={() => setCollapsed(true)} style={{ position: 'fixed', inset: 0, background: 'rgba(0,0,0,.45)', zIndex: 15 }} />
      )}
    </Layout>
  );
}

export default function App() {
  const [dark, setDarkState] = useState(loadDark);
  const [user, setUser] = useState<StaffUser | null>(null);
  const [checking, setChecking] = useState(!!getToken());

  const setDark = (value: boolean) => {
    setDarkState(value);
    try {
      localStorage.setItem('zm_admin_dark', value ? '1' : '0');
    } catch {
      /* */
    }
  };

  useEffect(() => {
    setUnauthorizedHandler(() => setUser(null));
    if (!getToken()) return;
    api<{ user: StaffUser }>('me/')
      .then((r) => setUser(r.user))
      .catch(() => setUser(null))
      .finally(() => setChecking(false));
  }, []);

  const auth = useMemo(() => ({ user }), [user]);
  const logout = () => {
    setToken('');
    setUser(null);
  };

  return (
    <ConfigProvider
      locale={mnMN}
      theme={{
        algorithm: dark ? theme.darkAlgorithm : theme.defaultAlgorithm,
        token: {
          colorPrimary: GOLD,
          colorInfo: '#3b8fd9',
          borderRadius: 10,
          fontFamily: "Inter, 'Segoe UI', Roboto, Arial, sans-serif",
          ...(dark ? { colorBgLayout: '#0b0e13', colorBgContainer: '#141922', colorBgElevated: '#1a202b' } : { colorBgLayout: '#f4f5f7' }),
        },
        components: {
          Menu: { darkItemBg: 'transparent', darkItemSelectedBg: 'rgba(240,176,48,.16)', darkItemSelectedColor: GOLD },
          Button: { primaryColor: '#1a1406' },
          Table: { headerBg: dark ? '#181e28' : '#fafafa' },
        },
      }}
    >
      <AntApp>
        <AuthContext.Provider value={auth}>
          {checking ? (
            <div style={{ minHeight: '100vh', display: 'grid', placeItems: 'center' }}>
              <Spin size="large" />
            </div>
          ) : user ? (
            <Shell user={user} onLogout={logout} dark={dark} setDark={setDark} />
          ) : (
            <LoginPage onLogin={setUser} />
          )}
        </AuthContext.Provider>
      </AntApp>
    </ConfigProvider>
  );
}
