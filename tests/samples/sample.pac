/* eslint-disable no-unused-vars */
/* global shExpMatch */
/**
* @param {String} [url] site url
* @param {String} [host] site host
*/
function FindProxyForURL(url, host)
{
    if (shExpMatch(host, '*discord.media'))
    {
        return 'DIRECT';
    }
    return 'SOCKS5 127.0.0.1:1086';
}