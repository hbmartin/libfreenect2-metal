# Injects the doxygen-awesome-css JavaScript extensions into a Doxygen HTML
# header. The header is generated fresh from the installed Doxygen version
# (via `doxygen -w html`) so the template always matches the toolchain, then
# this script splices in the theme's optional features (dark-mode toggle,
# copy-to-clipboard buttons, paragraph permalinks, interactive table of
# contents, tabbed content).
#
# Invoked as:
#   cmake -DINPUT=<raw header> -DOUTPUT=<patched header> -P inject_awesome.cmake

file(READ "${INPUT}" HEADER)

set(AWESOME_SCRIPTS
"<script type=\"text/javascript\" src=\"$relpath^doxygen-awesome-darkmode-toggle.js\"></script>
<script type=\"text/javascript\" src=\"$relpath^doxygen-awesome-fragment-copy-button.js\"></script>
<script type=\"text/javascript\" src=\"$relpath^doxygen-awesome-paragraph-link.js\"></script>
<script type=\"text/javascript\" src=\"$relpath^doxygen-awesome-interactive-toc.js\"></script>
<script type=\"text/javascript\" src=\"$relpath^doxygen-awesome-tabs.js\"></script>
<script type=\"text/javascript\">
  DoxygenAwesomeDarkModeToggle.init()
  DoxygenAwesomeFragmentCopyButton.init()
  DoxygenAwesomeParagraphLink.init()
  DoxygenAwesomeInteractiveToc.init()
  DoxygenAwesomeTabs.init()
</script>
</head>")

string(REPLACE "</head>" "${AWESOME_SCRIPTS}" HEADER "${HEADER}")

file(WRITE "${OUTPUT}" "${HEADER}")
