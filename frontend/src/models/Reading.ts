import mongoose from 'mongoose';

const ReadingSchema = new mongoose.Schema({
  temperature: Number,
  humidity: Number,
  timestamp: { type: Date, default: Date.now }
});

export default mongoose.models.Reading || mongoose.model('Reading', ReadingSchema); 