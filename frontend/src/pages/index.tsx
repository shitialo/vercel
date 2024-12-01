import React, { useEffect, useState } from 'react';
import { io } from 'socket.io-client';
import { Card, Title, LineChart } from '@tremor/react';
import { format } from 'date-fns';

interface Reading {
  temperature: number;
  humidity: number;
  timestamp: string;
}

export default function Home() {
  const [readings, setReadings] = useState<Reading[]>([]);

  useEffect(() => {
    const socket = io();

    socket.on('newReading', (data: Reading) => {
      setReadings(prev => [...prev, data].slice(-50));
    });

    return () => {
      socket.disconnect();
    };
  }, []);

  return (
    <main className="p-4 md:p-10 mx-auto max-w-7xl">
      <Title>Sensor Readings</Title>
      <Card className="mt-6">
        <LineChart
          data={readings}
          index="timestamp"
          categories={["temperature", "humidity"]}
          colors={["blue", "green"]}
          yAxisWidth={40}
          onValueChange={(v) => console.log(v)}
        />
      </Card>
    </main>
  );
} 