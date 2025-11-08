const express = require('express');
const router = express.Router();
const auth = require('../middleware/auth');
const authorize = require('../middleware/role'); // Import the role middleware

// @route   GET api/portfolio
// @desc    Get all portfolio items (public route)
// @access  Public
router.get('/', async (req, res) => {
    try {
        // In a real application, you would fetch public portfolio items from the database
        const portfolioItems = [
            { id: 1, title: 'Public Project Alpha', description: 'A cool public project.' },
            { id: 2, title: 'Public Project Beta', description: 'Another cool public project.' }
        ];
        res.json(portfolioItems);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   GET api/portfolio/me
// @desc    Get authenticated user's portfolio items
// @access  Private
router.get('/me', auth, async (req, res) => {
    try {
        // In a real application, you would fetch portfolio items from the database
        // based on the authenticated user (req.user.id)
        const userPortfolioItems = [
            { id: 101, title: 'My Private Project X', description: 'Details for my project.' },
            { id: 102, title: 'My Private Project Y', description: 'More details for my project.' }
        ];
        res.json(userPortfolioItems);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   POST api/portfolio
// @desc    Add a new portfolio item (Admin only)
// @access  Private (Admin)
router.post('/', auth, authorize(['admin']), async (req, res) => {
    try {
        const { title, description } = req.body;
        // In a real application, save the new portfolio item to the database
        const newPortfolioItem = { id: Math.floor(Math.random() * 1000), title, description };
        res.status(201).json(newPortfolioItem);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   PUT api/portfolio/:id
// @desc    Update a portfolio item (Admin only)
// @access  Private (Admin)
router.put('/:id', auth, authorize(['admin']), async (req, res) => {
    try {
        const { id } = req.params;
        const { title, description } = req.body;
        // In a real application, find and update the portfolio item in the database
        const updatedPortfolioItem = { id, title, description };
        res.json(updatedPortfolioItem);
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

// @route   DELETE api/portfolio/:id
// @desc    Delete a portfolio item (Admin only)
// @access  Private (Admin)
router.delete('/:id', auth, authorize(['admin']), async (req, res) => {
    try {
        const { id } = req.params;
        // In a real application, find and delete the portfolio item from the database
        res.json({ msg: `Portfolio item ${id} deleted` });
    } catch (err) {
        console.error(err.message);
        res.status(500).send('Server Error');
    }
});

module.exports = router;