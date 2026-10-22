import { Card } from 'antd';
import EventTable from '../components/EventTable';

export default function EventsPage() {
  return (
    <>
      <div className="page-head">
        <div>
          <h1>Үйлдлийн лог</h1>
          <p>Тоглогч ба админы бүх үйлдэл (засагдахгүй)</p>
        </div>
      </div>
      <Card variant="borderless">
        <EventTable />
      </Card>
    </>
  );
}
