const mongoose = require('mongoose');

const DailyRoutineSchema = new mongoose.Schema({
    user: {
        type: mongoose.Schema.Types.ObjectId,
        ref: 'user',
        required: true
    },
    date: {
        type: Date,
        default: Date.now
    },
    activities: [
        {
            name: {
                type: String,
                required: true
            },
            time: {
                type: String, // e.g., "08:00 AM"
                required: true
            },
            completed: {
                type: Boolean,
                default: false
            }
        }
    ]
});

module.exports = mongoose.model('dailyRoutine', DailyRoutineSchema);