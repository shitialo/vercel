import { Server as SocketIOServer } from 'socket.io';
import { NextApiRequest, NextApiResponse } from 'next';
import dbConnect from '@/lib/mongodb';
import Reading from '@/models/Reading';

interface CustomResponse extends NextApiResponse {
  socket: {
    server: {
      io?: SocketIOServer;
    };
  };
}

const SocketHandler = async (req: NextApiRequest, res: CustomResponse) => {
  if (res.socket.server.io) {
    console.log('Socket already running');
    return res.end();
  }

  const io = new SocketIOServer(res.socket.server);
  res.socket.server.io = io;

  await dbConnect();

  io.on('connection', socket => {
    console.log('Client connected');

    socket.on('sensorData', async (data) => {
      try {
        const reading = new Reading(data);
        await reading.save();
        io.emit('newReading', data);
      } catch (error) {
        console.error('Error saving reading:', error);
      }
    });
  });

  res.end();
};

export default SocketHandler; 