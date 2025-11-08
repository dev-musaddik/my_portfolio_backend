const express = require('express');
const router = express.Router();
const bcrypt = require('bcryptjs');
const jwt = require('jsonwebtoken');
const { check, validationResult } = require('express-validator');
const auth = require('../middleware/auth');
const User = require('../models/User');

// @route   POST api/auth/register
// @desc    Register user
// @access  Public
router.post(
    '/register',
    [
        check('name', 'Name is required').not().isEmpty(),
        check('email', 'Please include a valid email').isEmail(),
        check(
            'password',
            'Please enter a password with 6 or more characters'
        ).isLength({ min: 6 })
    ],
    async (req, res) => {
        console.debug('Registering new user...');

        const errors = validationResult(req);
        if (!errors.isEmpty()) {
            console.debug('Validation failed:', errors.array());
            return res.status(400).json({ errors: errors.array() });
        }

        const { name, email, password } = req.body;

        try {
            console.debug(`Checking if user with email: ${email} already exists...`);
            let user = await User.findOne({ email });

            if (user) {
                console.debug('User already exists');
                return res.status(400).json({ msg: 'User already exists' });
            }

            console.debug('Creating new user...');
            user = new User({
                name,
                email,
                password
            });

            const salt = await bcrypt.genSalt(10);
            console.debug('Hashing password...');
            user.password = await bcrypt.hash(password, salt);

            await user.save();
            console.debug('User saved to database.');

            const payload = {
                user: {
                    id: user.id,
                    role: user.role
                }
            };

            console.debug('Generating JWT token...');
            jwt.sign(
                payload,
                process.env.JWT_SECRET,
                { expiresIn: '5h' },
                (err, token) => {
                    if (err) {
                        console.error('Error generating token:', err);
                        throw err;
                    }
                    console.debug('Token generated successfully.');
                    res.json({ token });
                }
            );
        } catch (err) {
            console.error('Server error during registration:', err.message);
            res.status(500).send('Server error');
        }
    }
);

// @route   POST api/auth/login
// @desc    Authenticate user & get token
// @access  Public
router.post(
    '/login',
    [
        check('email', 'Please include a valid email').isEmail(),
        check('password', 'Password is required').exists()
    ],
    async (req, res) => {
        console.debug('Logging in user...');

        const errors = validationResult(req);
        if (!errors.isEmpty()) {
            console.debug('Validation failed:', errors.array());
            return res.status(400).json({ errors: errors.array() });
        }

        const { email, password } = req.body;

        try {
            console.debug(`Checking if user with email: ${email} exists...`);
            let user = await User.findOne({ email });

            if (!user) {
                console.debug('User not found.');
                return res.status(400).json({ msg: 'Invalid Credentials' });
            }

            console.debug('Comparing password...');
            const isMatch = await bcrypt.compare(password, user.password);

            if (!isMatch) {
                console.debug('Password does not match.');
                return res.status(400).json({ msg: 'Invalid Credentials' });
            }

            const payload = {
                user: {
                    id: user.id,
                    role: user.role
                }
            };

            console.debug('Generating JWT token...');
            jwt.sign(
                payload,
                process.env.JWT_SECRET,
                { expiresIn: '5h' },
                (err, token) => {
                    if (err) {
                        console.error('Error generating token:', err);
                        throw err;
                    }
                    console.debug('Token generated successfully.');
                    res.json({ token });
                }
            );
        } catch (err) {
            console.error('Server error during login:', err.message);
            res.status(500).send('Server error');
        }
    }
);

// @route   GET api/auth
// @desc    Get user by token
// @access  Private
router.get('/', auth, async (req, res) => {
    console.debug('Fetching user data from token...');

    try {
        const user = await User.findById(req.user.id).select('-password');
        if (!user) {
            console.debug('User not found.');
            return res.status(404).json({ msg: 'User not found' });
        }
        console.debug('User found, sending response...');
        res.json(user);
    } catch (err) {
        console.error('Error fetching user data:', err.message);
        res.status(500).send('Server Error');
    }
});

module.exports = router;
