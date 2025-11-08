const express = require('express');
const router = express.Router();
const auth = require('../middleware/auth');
const authorize = require('../middleware/role');
const Blog = require('../models/Blog'); // Import the Blog model
const multer = require('multer');
const path = require('path');

// Multer configuration for image uploads
const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        cb(null, 'uploads/'); // Images will be stored in the 'uploads' directory
    },
    filename: (req, file, cb) => {
        cb(null, `${file.fieldname}-${Date.now()}${path.extname(file.originalname)}`);
    }
});

const upload = multer({
    storage: storage,
    limits: { fileSize: 1000000 }, // 1MB file size limit
    fileFilter: (req, file, cb) => {
        checkFileType(file, cb);
    }
});

// Check file type
function checkFileType(file, cb) {
    // Allowed ext
    const filetypes = /jpeg|jpg|png|gif/;
    // Check ext
    const extname = filetypes.test(path.extname(file.originalname).toLowerCase());
    // Check mime
    const mimetype = filetypes.test(file.mimetype);

    if (mimetype && extname) {
        return cb(null, true);
    } else {
        cb('Error: Images Only!');
    }
}

// @route   GET api/blog
// @desc    Get all blogs (public)
// @access  Public
router.get('/', async (req, res) => {
    try {
        const blogs = await Blog.find().populate('author', ['name', 'role']).sort({ date: -1 });
        res.json(blogs);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   GET api/blog/:id
// @desc    Get a single blog by ID (public)
// @access  Public
router.get('/:id', async (req, res) => {
    try {
        const blog = await Blog.findById(req.params.id).populate('author', ['name', 'role']);
        if (!blog) {
            return res.status(404).json({ msg: 'Blog not found' });
        }
        res.json(blog);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   POST api/blog
// @desc    Create a new blog post (Admin only)
// @access  Private (Admin)
router.post('/', auth, authorize(['admin']), upload.single('image'), async (req, res) => {
    try {
        const { title, content } = req.body;
        const newBlog = new Blog({
            title,
            content,
            author: req.user.id,
            image: req.file ? `/uploads/${req.file.filename}` : null // Save image path
        });

        const blog = await newBlog.save();
        res.json(blog);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   PUT api/blog/:id
// @desc    Update a blog post (Admin only)
// @access  Private (Admin)
router.put('/:id', auth, authorize(['admin']), upload.single('image'), async (req, res) => {
    try {
        const { title, content } = req.body;
        let blog = await Blog.findById(req.params.id);

        if (!blog) {
            return res.status(404).json({ msg: 'Blog not found' });
        }

        // Update fields
        blog.title = title || blog.title;
        blog.content = content || blog.content;
        if (req.file) {
            blog.image = `/uploads/${req.file.filename}`;
        }

        await blog.save();
        res.json(blog);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   DELETE api/blog/:id
// @desc    Delete a blog post (Admin only)
// @access  Private (Admin)
router.delete('/:id', auth, authorize(['admin']), async (req, res) => {
    try {
        const blog = await Blog.findById(req.params.id);

        if (!blog) {
            return res.status(404).json({ msg: 'Blog not found' });
        }

        await Blog.deleteOne({ _id: req.params.id });
        res.json({ msg: 'Blog removed' });
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

module.exports = router;