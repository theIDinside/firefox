/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.search.telemetry.incontent

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import mozilla.components.feature.search.telemetry.ExtensionInfo
import mozilla.components.feature.search.telemetry.SearchProviderCookie
import mozilla.components.feature.search.telemetry.SearchProviderModel
import mozilla.components.feature.search.telemetry.incontent.InContentTelemetry.Companion.SEARCH_EXTENSION_ID
import mozilla.components.feature.search.telemetry.incontent.InContentTelemetry.Companion.SEARCH_EXTENSION_RESOURCE_URL
import mozilla.components.feature.search.telemetry.incontent.InContentTelemetry.Companion.SEARCH_MESSAGE_ID
import mozilla.components.feature.search.telemetry.incontent.InContentTelemetry.Companion.SEARCH_MESSAGE_LIST_KEY
import mozilla.components.feature.search.telemetry.incontent.InContentTelemetry.Companion.SEARCH_MESSAGE_SESSION_URL_KEY
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.FactProcessor
import mozilla.components.support.base.facts.Facts
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class InContentTelemetryTest {
    private lateinit var telemetry: InContentTelemetry

    fun createMockProviderList(): List<SearchProviderModel> = listOf(
        SearchProviderModel(
            schema = 1671479978127,
            taggedCodes = listOf("monline_7_dg", "monline_4_dg", "monline_3_dg", "monline_dg"),
            telemetryId = "baidu",
            organicCodes = emptyList(),
            codeParamName = "tn",
            followOnParamNames = listOf("oq"),
            queryParamNames = listOf("wd", "word"),
            searchPageRegexp = "^https://(?:m|www)\\.baidu\\.com/(?:s|baidu)",
            extraAdServersRegexps = listOf("^https?://www\\.baidu\\.com/baidu\\.php?"),
            expectedOrganicCodes = emptyList(),
        ),
        SearchProviderModel(
            schema = 1671479978127,
            taggedCodes = listOf("firefox-b-m", "fpas", "lm"),
            telemetryId = "example",
            organicCodes = listOf("foo"),
            codeParamName = "pc",
            queryParamNames = listOf("q"),
            searchPageRegexp = "^https:\\/\\/example\\.com\\/",
            extraAdServersRegexps = listOf("^https://example.com/y\\\\.js?.*ad_provider\\\\="),
            expectedOrganicCodes = emptyList(),
        ),
        SearchProviderModel(
            schema = 1671479978127,
            taggedCodes = listOf("firefox-b-m", "fpas", "lm"),
            telemetryId = "duckduckgo",
            organicCodes = emptyList(),
            codeParamName = "t",
            queryParamNames = listOf("q"),
            searchPageRegexp = "^https:\\/\\/duckduckgo\\.com\\/",
            extraAdServersRegexps = listOf("^https://duckduckgo.com/y\\\\.js?.*ad_provider\\\\="),
            expectedOrganicCodes = listOf("ha"),
        ),
        SearchProviderModel(
            schema = 1671479978127,
            taggedCodes = listOf("firefox-b-m", "fpas", "def"),
            telemetryId = "google",
            organicCodes = emptyList(),
            codeParamName = "client",
            followOnParamNames = listOf("oq", "ved", "ei"),
            queryParamNames = listOf("q"),
            searchPageRegexp = "^https://www\\.google\\.(?:.+)/search",
            extraAdServersRegexps = listOf("^https?://www\\\\.google(?:adservices)?\\\\.com/(?:pagead/)?aclk"),
            expectedOrganicCodes = emptyList(),
        ),
        SearchProviderModel(
            schema = 1671479978127,
            taggedCodes = listOf("MOZ2", "MOZL", "def"),
            telemetryId = "bing",
            organicCodes = emptyList(),
            codeParamName = "pc",
            queryParamNames = listOf("q"),
            searchPageRegexp = "^https://www\\.bing\\.com/search",
            extraAdServersRegexps = listOf("^https://www\\\\.bing\\\\.com/acli?c?k"),
            followOnCookies = listOf(
                SearchProviderCookie(
                    extraCodeParamName = "form",
                    extraCodePrefixes = listOf("QBRE"),
                    host = "name",
                    name = "SRCHS",
                    codeParamName = "PC",
                ),
            ),
            expectedOrganicCodes = emptyList(),
        ),
        SearchProviderModel(
            schema = 1671479978127,
            taggedCodes = listOf("MOZ2", "MOZL", "def"),
            telemetryId = "bing2",
            organicCodes = emptyList(),
            codeParamName = "pc",
            queryParamNames = listOf("q"),
            searchPageRegexp = "^https://www\\.bing2\\.com/search",
            extraAdServersRegexps = listOf("^https://www\\\\.bing2\\\\.com/acli?c?k"),
            followOnCookies = listOf(
                SearchProviderCookie(
                    extraCodeParamName = "",
                    extraCodePrefixes = emptyList(),
                    host = "name",
                    name = "SRCHS",
                    codeParamName = "PC",
                ),
            ),
            expectedOrganicCodes = emptyList(),
        ),
    )

    @Before
    fun setup() {
        telemetry = spy(InContentTelemetry())
    }

    @Test
    fun `WHEN installWebExtension is called THEN install a properly configured extension`() {
        val engine: Engine = mock()
        val store: BrowserStore = mock()
        val extensionCaptor = argumentCaptor<ExtensionInfo>()

        runBlocking {
            doNothing().`when`(telemetry).setProviderList(any())
            telemetry.install(engine, store, mock())
        }

        verify(telemetry).installWebExtension(eq(engine), eq(store), extensionCaptor.capture())
        assertEquals(SEARCH_EXTENSION_ID, extensionCaptor.value.id)
        assertEquals(SEARCH_EXTENSION_RESOURCE_URL, extensionCaptor.value.resourceUrl)
        assertEquals(SEARCH_MESSAGE_ID, extensionCaptor.value.messageId)
    }

    @Test
    fun `GIVEN a message from the extension WHEN processMessage is called THEN track the search`() {
        val first = JSONObject()
        val second = JSONObject()
        val array = JSONArray()
        array.put(first)
        array.put(second)
        val message = JSONObject()
        val url = "https://www.google.com/search?q=aaa"
        message.put(SEARCH_MESSAGE_LIST_KEY, array)
        message.put(SEARCH_MESSAGE_SESSION_URL_KEY, url)

        telemetry.processMessage(message)

        verify(telemetry).trackPartnerUrlTypeMetric(url, listOf(first, second))
    }

    @Test
    fun `GIVEN a Example search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://example.com/?q=aaa&pc=foo"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("example.in-content.organic.foo", facts[0].value)
    }

    @Test
    fun `GIVEN a Google search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.google.com/search?q=aaa&client=firefox-b-m"
        telemetry.providerList = createMockProviderList()

        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )
        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("google.in-content.sap.firefox-b-m", facts[0].value)
    }

    @Test
    fun `GIVEN a DuckDuckGo search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://duckduckgo.com/?q=aaa&t=fpas"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("duckduckgo.in-content.sap.fpas", facts[0].value)
    }

    @Test
    fun `GIVEN an invalid Bing search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.bing.com/search?q=aaa&pc=MOZMBA"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("bing.in-content.organic.other", facts[0].value)
    }

    @Test
    fun `GIVEN a Google sap-follow-on WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.google.com/search?q=aaa&client=firefox-b-m&oq=random"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("google.in-content.sap-follow-on.firefox-b-m", facts[0].value)
    }

    @Test
    fun `GIVEN an invalid Google sap-follow-on WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.google.com/search?q=aaa&client=firefox-b-mTesting&oq=random"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("google.in-content.organic.other", facts[0].value)
    }

    @Test
    fun `GIVEN a Google sap-follow-on from topSite WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.google.com/search?q=aaa&client=firefox-b-m&channel=ts&oq=random"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("google.in-content.sap-follow-on.firefox-b-m.ts", facts[0].value)
    }

    @Test
    fun `GIVEN an invalid Google channel from topSite WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.google.com/search?q=aaa&client=firefox-b-m&channel=tsTest&oq=random"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("google.in-content.sap-follow-on.firefox-b-m", facts[0].value)
    }

    @Test
    fun `GIVEN a Bing sap-follow-on with cookies WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.bing.com/search?q=aaa&form=QBRERANDOM"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, createCookieList())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("bing.in-content.sap-follow-on.mozl", facts[0].value)
    }

    @Test
    fun `GIVEN a Bing sap-follow-on with cookies AND form param is not in the URL when it is required WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.bing.com/search?q=aaa"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, createCookieList())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("bing.in-content.organic.none", facts[0].value)
    }

    @Test
    fun `GIVEN a Bing sap-follow-on with cookies AND form param is not required WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.bing2.com/search?q=aaa"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, createCookieList())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("bing2.in-content.sap-follow-on.mozl", facts[0].value)
    }

    @Test
    fun `GIVEN a Google organic search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.google.com/search?q=aaa"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("google.in-content.organic.none", facts[0].value)
    }

    @Test
    fun `GIVEN a DuckDuckGo organic search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://duckduckgo.com/?q=aaa"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("duckduckgo.in-content.organic.none", facts[0].value)
    }

    @Test
    fun `GIVEN a DuckDuckGo organic search with expected organic code WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://duckduckgo.com/?t=ha&q=aaa"
        val facts = mutableListOf<Fact>()
        telemetry.providerList = createMockProviderList()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("duckduckgo.in-content.organic.none", facts[0].value)
    }

    @Test
    fun `GIVEN a Bing organic search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://www.bing.com/search?q=aaa"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("bing.in-content.organic.none", facts[0].value)
    }

    @Test
    fun `GIVEN a Baidu organic search WHEN trackPartnerUrlTypeMetric is called THEN emit an appropriate IN_CONTENT_SEARCH fact`() {
        val url = "https://m.baidu.com/s?word=aaa"
        telemetry.providerList = createMockProviderList()
        val facts = mutableListOf<Fact>()
        Facts.registerProcessor(
            object : FactProcessor {
                override fun process(fact: Fact) {
                    facts.add(fact)
                }
            },
        )

        telemetry.trackPartnerUrlTypeMetric(url, listOf())

        assertEquals(1, facts.size)
        assertEquals(Component.FEATURE_SEARCH, facts[0].component)
        assertEquals(Action.INTERACTION, facts[0].action)
        assertEquals(InContentTelemetry.IN_CONTENT_SEARCH, facts[0].item)
        assertEquals("baidu.in-content.organic.none", facts[0].value)
    }

    private fun createCookieList(): List<JSONObject> {
        val first = JSONObject()
        first.put("name", "SRCHS")
        first.put("value", "PC=MOZL")
        val second = JSONObject()
        second.put("name", "RANDOM")
        second.put("value", "RANDOM")
        return listOf(first, second)
    }
}
