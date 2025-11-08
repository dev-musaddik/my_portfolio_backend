const mongoose = require('mongoose');

const SkillSchema = new mongoose.Schema({
    name: {
        type: String,
        required: true
    },
    level: {
        type: Number,
        required: true
    }
});

module.exports = mongoose.model('skill', SkillSchema);