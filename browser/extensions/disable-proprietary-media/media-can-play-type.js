// media-can-play-type.js
const orig = HTMLMediaElement.prototype.canPlayType;
HTMLMediaElement.prototype.canPlayType = function(type) {
    // Block proprietary codecs like MP4, AAC, and H.264
    if (/mp4|aac|h264/.test(type)) return "";
    return orig.call(this, type);
};
