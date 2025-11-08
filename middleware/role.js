module.exports = (roles) => (req, res, next) => {
    // roles is an array of roles allowed to access the route
    if (!roles.includes(req.user.role)) {
        return res.status(403).json({ msg: 'Authorization denied: Insufficient role' });
    }
    next();
};