const express = require('express');
const router = express.Router();
const auth = require('../middleware/auth');
const authorize = require('../middleware/role');
const multer = require('multer');
const { storage } = require('../config/cloudinary');
const upload = multer({ storage });
const User = require('../models/User');

// @route   PUT /api/profile/image
// @desc    Upload a profile image
// @access  Private
router.put('/image', [auth, authorize(['admin']), upload.single('profileImage')], async (req, res) => {
  try {
    const user = await User.findById(req.user.id);
    if (!user) {
      return res.status(404).json({ msg: 'User not found' });
    }

    user.profileImage = req.file.path;
    await user.save();

    res.json(user);
  } catch (err) {
    console.error(err.message);
    res.status(500).send('Server Error');
  }
});

module.exports = router;
