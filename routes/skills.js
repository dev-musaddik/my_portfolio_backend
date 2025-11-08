const express = require('express');
const router = express.Router();
const auth = require('../middleware/auth');
const authorize = require('../middleware/role');
const Skill = require('../models/Skill');

// @route   GET api/skills
// @desc    Get all skills
// @access  Public
router.get('/', async (req, res) => {
    try {
        const skills = await Skill.find();
        res.json(skills);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   POST api/skills
// @desc    Create a new skill
// @access  Private (Admin)
router.post('/', auth, authorize(['admin']), async (req, res) => {
    try {
        const { name, level } = req.body;
        const newSkill = new Skill({
            name,
            level
        });

        const skill = await newSkill.save();
        res.json(skill);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   PUT api/skills/:id
// @desc    Update a skill
// @access  Private (Admin)
router.put('/:id', auth, authorize(['admin']), async (req, res) => {
    try {
        const { name, level } = req.body;
        let skill = await Skill.findById(req.params.id);

        if (!skill) {
            return res.status(404).json({ msg: 'Skill not found' });
        }

        skill.name = name || skill.name;
        skill.level = level || skill.level;

        await skill.save();
        res.json(skill);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   DELETE api/skills/:id
// @desc    Delete a skill
// @access  Private (Admin)
router.delete('/:id', auth, authorize(['admin']), async (req, res) => {
    try {
        const skill = await Skill.findById(req.params.id);

        if (!skill) {
            return res.status(404).json({ msg: 'Skill not found' });
        }

        await Skill.deleteOne({ _id: req.params.id });
        res.json({ msg: 'Skill removed' });
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

module.exports = router;