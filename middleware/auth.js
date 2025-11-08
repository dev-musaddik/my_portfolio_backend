const jwt = require('jsonwebtoken');

module.exports = function (req, res, next) {
    // Get token from header
    const token = req.header('x-auth-token');
    console.log('[AuthMiddleware] Incoming request:', req.method, req.originalUrl);
    console.log('[AuthMiddleware] Token from header:', token ? 'Present' : 'Missing');

    // Check if not token
    if (!token) {
        console.warn('[AuthMiddleware] No token provided. Authorization denied.');
        return res.status(401).json({ msg: 'No token, authorization denied' });
    }

    // Verify token
    try {
        const decoded = jwt.verify(token, process.env.JWT_SECRET);
        console.log('[AuthMiddleware] Token successfully verified. Decoded payload:', decoded);

        req.user = decoded.user;
        next();
    } catch (err) {
        console.error('[AuthMiddleware] Token verification failed:', err.message);
        res.status(401).json({ msg: 'Token is not valid' });
    }
};
