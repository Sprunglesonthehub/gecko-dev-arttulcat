// background.js
chrome.tabs.onCreated.addListener(function(tab) {
    chrome.tabs.executeScript(tab.id, {file: 'media-can-play-type.js'});
});
