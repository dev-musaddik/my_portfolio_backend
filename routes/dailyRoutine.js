const express = require('express');
const router = express.Router();
const auth = require('../middleware/auth');
const authorize = require('../middleware/role');
const DailyRoutine = require('../models/DailyRoutine');

// @route   GET api/daily-routine/me
// @desc    Get authenticated user's daily routine
// @access  Private
router.get('/me', async (req, res) => {
    try {
        const dailyRoutine = await DailyRoutine.findOne({  });
        if (!dailyRoutine) {
            return res.status(404).json({ msg: 'Daily routine not found for this user' });
        }
        res.json(dailyRoutine);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   POST api/daily-routine
// @desc    Create or update daily routine (Admin only)
// @access  Private (Admin)
router.post('/', authorize(['admin']), async (req, res) => {
    const { user, date, activities } = req.body;

    // Build daily routine object
    const dailyRoutineFields = {};
    if (user) dailyRoutineFields.user = user;
    if (date) dailyRoutineFields.date = date;
    if (activities) dailyRoutineFields.activities = activities;

    try {
        let dailyRoutine = await DailyRoutine.findOneAndUpdate(
            { user: dailyRoutineFields.user }, // Find by user
            { $set: dailyRoutineFields },
            { new: true, upsert: true, setDefaultsOnInsert: true } // Create if not found
        );
        res.json(dailyRoutine);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   DELETE api/daily-routine/:id
// @desc    Delete a daily routine (Admin only)
// @access  Private (Admin)
router.delete('/:id', authorize(['admin']), async (req, res) => {
    try {
        const dailyRoutine = await DailyRoutine.findById(req.params.id);

        if (!dailyRoutine) {
            return res.status(404).json({ msg: 'Daily routine not found' });
        }

        await DailyRoutine.deleteOne({ _id: req.params.id });
        res.json({ msg: 'Daily routine removed' });
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

module.exports = router;