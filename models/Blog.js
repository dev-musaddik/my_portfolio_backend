const mongoose = require('mongoose');

const BlogSchema = new mongoose.Schema({
    title: {
        type: String,
        required: true
    },
    content: {
        type: String,
        required: true
    },
    author: {
        type: mongoose.Schema.Types.ObjectId,
        ref: 'user'
    },
    image: {
        type: String // Store the path to the image
    },
    date: {
        type: Date,
        default: Date.now
    }
});

module.exports = mongoose.model('blog', BlogSchema);