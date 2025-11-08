const express = require('express');
const router = express.Router();
const auth = require('../middleware/auth');
const authorize = require('../middleware/role');
const Project = require('../models/Project');
const multer = require('multer');
const path = require('path');

// Multer configuration for image uploads
const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        cb(null, 'uploads/');
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
    const filetypes = /jpeg|jpg|png|gif/;
    const extname = filetypes.test(path.extname(file.originalname).toLowerCase());
    const mimetype = filetypes.test(file.mimetype);

    if (mimetype && extname) {
        return cb(null, true);
    } else {
        cb('Error: Images Only!');
    }
}

// @route   GET api/projects
// @desc    Get all projects
// @access  Public
router.get('/', async (req, res) => {
    try {
        const projects = await Project.find().sort({ date: -1 });
        res.json(projects);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   POST api/projects
// @desc    Create a new project
// @access  Private (Admin)
router.post('/', auth, authorize(['admin']), upload.single('image'), async (req, res) => {
    try {
        const { title, description, liveUrl, githubUrl } = req.body;
        const newProject = new Project({
            title,
            description,
            liveUrl,
            githubUrl,
            imageUrl: req.file ? `/uploads/${req.file.filename}` : null
        });

        const project = await newProject.save();
        res.json(project);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   PUT api/projects/:id
// @desc    Update a project
// @access  Private (Admin)
router.put('/:id', auth, authorize(['admin']), upload.single('image'), async (req, res) => {
    try {
        const { title, description, liveUrl, githubUrl } = req.body;
        let project = await Project.findById(req.params.id);

        if (!project) {
            return res.status(404).json({ msg: 'Project not found' });
        }

        project.title = title || project.title;
        project.description = description || project.description;
        project.liveUrl = liveUrl || project.liveUrl;
        project.githubUrl = githubUrl || project.githubUrl;
        if (req.file) {
            project.imageUrl = `/uploads/${req.file.filename}`;
        }

        await project.save();
        res.json(project);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   DELETE api/projects/:id
// @desc    Delete a project
// @access  Private (Admin)
router.delete('/:id', auth, authorize(['admin']), async (req, res) => {
    try {
        const project = await Project.findById(req.params.id);

        if (!project) {
            return res.status(404).json({ msg: 'Project not found' });
        }

        await Project.deleteOne({ _id: req.params.id });
        res.json({ msg: 'Project removed' });
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

module.exports = router;