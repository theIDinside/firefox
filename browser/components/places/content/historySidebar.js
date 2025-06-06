/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Shared Places Import - change other consumers if you change this: */
var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

XPCOMUtils.defineLazyScriptGetter(
  this,
  "PlacesTreeView",
  "chrome://browser/content/places/treeView.js"
);
XPCOMUtils.defineLazyScriptGetter(
  this,
  ["PlacesInsertionPoint", "PlacesController", "PlacesControllerDragHelper"],
  "chrome://browser/content/places/controller.js"
);
/* End Shared Places Import */

var gHistoryTree;
var gSearchBox;
var gHistoryGrouping = "";
var gCumulativeSearches = 0;
var gCumulativeFilterCount = 0;

window.addEventListener("load", () => {
  let uidensity = window.top.document.documentElement.getAttribute("uidensity");
  if (uidensity) {
    document.documentElement.setAttribute("uidensity", uidensity);
  }

  gHistoryTree = document.getElementById("historyTree");
  gHistoryTree.addEventListener("click", event =>
    PlacesUIUtils.onSidebarTreeClick(event)
  );
  gHistoryTree.addEventListener("keypress", event =>
    PlacesUIUtils.onSidebarTreeKeyPress(event)
  );
  gHistoryTree.addEventListener("mousemove", event =>
    PlacesUIUtils.onSidebarTreeMouseMove(event)
  );
  gHistoryTree.addEventListener("mouseout", () =>
    PlacesUIUtils.setMouseoverURL("", window)
  );

  gSearchBox = document.getElementById("search-box");
  gSearchBox.addEventListener("MozInputSearch:search", () =>
    searchHistory(gSearchBox.value)
  );

  let viewButton = document.getElementById("viewButton");
  gHistoryGrouping = viewButton.getAttribute("selectedsort");

  Glean.historySidebar.filterType[gHistoryGrouping].add(1);

  if (gHistoryGrouping == "site") {
    document.getElementById("bysite").setAttribute("checked", "true");
  } else if (gHistoryGrouping == "visited") {
    document.getElementById("byvisited").setAttribute("checked", "true");
  } else if (gHistoryGrouping == "lastvisited") {
    document.getElementById("bylastvisited").setAttribute("checked", "true");
  } else if (gHistoryGrouping == "dayandsite") {
    document.getElementById("bydayandsite").setAttribute("checked", "true");
  } else {
    document.getElementById("byday").setAttribute("checked", "true");
  }

  document
    .querySelector("#viewButton > menupopup")
    .addEventListener("command", event => {
      let by = event.target.id.slice(2);
      viewButton.setAttribute("selectedsort", by);
      GroupBy(by);
    });

  let bhTooltip = document.getElementById("bhTooltip");
  bhTooltip.addEventListener("popupshowing", event => {
    window.top.BookmarksEventHandler.fillInBHTooltip(bhTooltip, event);
  });
  bhTooltip.addEventListener("popuphiding", () =>
    bhTooltip.removeAttribute("position")
  );

  searchHistory("");
});

function GroupBy(groupingType) {
  if (groupingType != gHistoryGrouping) {
    Glean.historySidebar.filterType[groupingType].add(1);
  }
  gHistoryGrouping = groupingType;
  gCumulativeFilterCount++;
  searchHistory(gSearchBox.value);
}

function updateTelemetry(urlsOpened = []) {
  Glean.historySidebar.cumulativeSearches.accumulateSingleSample(
    gCumulativeSearches
  );
  Glean.historySidebar.cumulativeFilterCount.accumulateSingleSample(
    gCumulativeFilterCount
  );
  clearCumulativeCounters();

  Glean.sidebar.link.history.add(urlsOpened.length);
}

function searchHistory(aInput) {
  var query = PlacesUtils.history.getNewQuery();
  var options = PlacesUtils.history.getNewQueryOptions();

  const NHQO = Ci.nsINavHistoryQueryOptions;
  var sortingMode;
  var resultType;

  switch (gHistoryGrouping) {
    case "visited":
      resultType = NHQO.RESULTS_AS_URI;
      sortingMode = NHQO.SORT_BY_VISITCOUNT_DESCENDING;
      break;
    case "lastvisited":
      resultType = NHQO.RESULTS_AS_URI;
      sortingMode = NHQO.SORT_BY_DATE_DESCENDING;
      break;
    case "dayandsite":
      resultType = NHQO.RESULTS_AS_DATE_SITE_QUERY;
      break;
    case "site":
      resultType = NHQO.RESULTS_AS_SITE_QUERY;
      sortingMode = NHQO.SORT_BY_TITLE_ASCENDING;
      break;
    case "day":
    default:
      resultType = NHQO.RESULTS_AS_DATE_QUERY;
      break;
  }

  if (aInput) {
    query.searchTerms = aInput;
    if (gHistoryGrouping != "visited" && gHistoryGrouping != "lastvisited") {
      sortingMode = NHQO.SORT_BY_FRECENCY_DESCENDING;
      resultType = NHQO.RESULTS_AS_URI;
    }
  }

  options.sortingMode = sortingMode;
  options.resultType = resultType;
  options.includeHidden = !!aInput;

  let timerId;
  if (gHistoryGrouping == "lastvisited") {
    timerId = Glean.historySidebar.lastvisitedTreeQueryTime.start();
  }

  // call load() on the tree manually
  // instead of setting the place attribute in historySidebar.xhtml
  // otherwise, we will end up calling load() twice
  gHistoryTree.load(query, options);

  // Sometimes search is activated without an input string. For example, when
  // the history sidbar is first opened or when a search filter is selected.
  // Since we're trying to measure how often the searchbar was used, we should first
  // check if there's an input string before collecting telemetry.
  if (aInput) {
    Glean.sidebar.search.history.add(1);
    gCumulativeSearches++;
  }

  if (gHistoryGrouping == "lastvisited") {
    Glean.historySidebar.lastvisitedTreeQueryTime.stopAndAccumulate(timerId);
  }
}

function clearCumulativeCounters() {
  gCumulativeSearches = 0;
  gCumulativeFilterCount = 0;
}

window.addEventListener("unload", () => {
  clearCumulativeCounters();
  PlacesUIUtils.setMouseoverURL("", window);
});

window.addEventListener("SidebarFocused", () => gSearchBox.focus());
