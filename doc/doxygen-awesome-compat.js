// Doxygen 1.17 no longer emits jQuery, while Doxygen Awesome v2.4.2 still uses
// its ready() and resize() helpers in three optional extensions. Supply only
// that tiny surface so the vendored upstream files can remain unmodified.
(function() {
    "use strict"

    if (window.$) {
        return
    }

    function onReady(callback) {
        if (document.readyState === "loading") {
            document.addEventListener("DOMContentLoaded", callback, { once: true })
        } else {
            callback()
        }
    }

    window.$ = function(target) {
        if (typeof target === "function") {
            onReady(target)
            return undefined
        }

        return {
            ready: onReady,
            resize: function(callback) {
                window.addEventListener("resize", callback)
            }
        }
    }

    window.DoxygenAwesomeCompatibility = {
        initInteractiveToc: function() {
            // Doxygen 1.17 can preserve an external issue link inside a TOC
            // entry. The upstream extension treats every TOC anchor as a
            // local fragment and dereferences a missing element. Convert only
            // those nested, non-fragment links to text before its load handler
            // scans the TOC; the surrounding section link remains clickable.
            window.addEventListener("load", function() {
                document.querySelectorAll(".contents > .toc > ul a[href]").forEach(function(link) {
                    const href = link.getAttribute("href")
                    const target = href && href.startsWith("#")
                        ? document.getElementById(href.substring(1))
                        : null
                    if (!target) {
                        const label = document.createElement("span")
                        label.innerHTML = link.innerHTML
                        link.replaceWith(label)
                    }
                })
            })
            DoxygenAwesomeInteractiveToc.init()
        }
    }
})()
