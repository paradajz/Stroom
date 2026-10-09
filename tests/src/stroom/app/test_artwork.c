#include "app/artwork.h"
#include "ui/artwork/view.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

static AudioSourceStatus source;
static TrackMetadata     prepared;
static ArtworkIdentity   prepared_identity;
static unsigned          network_takes, cd_takes;
static int               download, accept, has_blob, decoder_available;
static void*             downloaded;
static void*             accepted;
static unsigned          released;
static int               close_ok;
#if STROOM_DIAGNOSTICS
static ArtworkObserver    registered_observer;
static ArtworkObservation observation;
static unsigned           observations;
#endif

void __real_free(void* pointer);

void __wrap_free(void* pointer)
{
    if (pointer && pointer == downloaded)
    {
        ++released;
    }

    __real_free(pointer);
}

int ui_artwork_open(void)
{
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NOT_NULL(registered_observer);
#endif

    return decoder_available ? 0 : -1;
}

int ui_artwork_close(void)
{
    if (!close_ok)
    {
        return -1;
    }

    free(accepted);

    accepted = NULL;

    return 0;
}

static int take(const TrackMetadata* metadata, ArtworkBlob* blob)
{
    TEST_ASSERT_EQUAL_MEMORY(&source.metadata, metadata, sizeof(*metadata));

    if (!download)
    {
        return 0;
    }

    downloaded = malloc(1);

    TEST_ASSERT_NOT_NULL(downloaded);

    *blob = (ArtworkBlob){ .data = downloaded, .size = 1, .metadata = *metadata };

    return 1;
}

int network_take_artwork(const TrackMetadata* metadata, ArtworkBlob* blob)
{
    ++network_takes;

    return (take(metadata, blob)) ? 0 : 1;
}

int cd_lookup_take_artwork(const TrackMetadata* metadata, ArtworkBlob* blob)
{
    ++cd_takes;

    return (take(metadata, blob)) ? 0 : 1;
}

void ui_artwork_prepare(const TrackMetadata* metadata, ArtworkBlob* blob, ArtworkIdentity identity)
{
    prepared          = *metadata;
    prepared_identity = identity;
    has_blob          = blob != NULL;

    if (accept && blob && blob->data)
    {
        accepted = blob->data;

        memset(blob, 0, sizeof(*blob));
    }
}

#if STROOM_DIAGNOSTICS
void ui_artwork_set_observer(ArtworkObserver observer)
{
    registered_observer = observer;
}

void network_artwork_observe(const TrackMetadata* metadata, ArtworkObservation event)
{
    TEST_ASSERT_EQUAL_MEMORY(&source.metadata, metadata, sizeof(*metadata));

    observation = event;

    ++observations;
}
#endif

void setUp(void)
{
    source = (AudioSourceStatus){ .kind = AUDIO_SOURCE_NETWORK };

    strcpy(source.metadata.title, "Track");
    strcpy(source.metadata.artwork_url, "http://1.2.3.4/cover");

    network_takes = cd_takes = released = 0;
    download = accept = has_blob = 0;
    downloaded = accepted = NULL;
    decoder_available     = 1;
    close_ok              = 1;
#if STROOM_DIAGNOSTICS
    observations = 0;
#endif
    TEST_ASSERT_TRUE(app_artwork_open() == 0);
}

void tearDown(void)
{
    app_artwork_close();
}

static void source_identity_and_ownership(void)
{
    source.kind       = AUDIO_SOURCE_CD;
    source.cd.present = 1;
    download = accept = 1;

    app_artwork_prepare(&source);
    TEST_ASSERT_EQUAL_UINT(1, cd_takes);
    TEST_ASSERT_EQUAL_UINT(0, network_takes);
    TEST_ASSERT_EQUAL_INT(ARTWORK_PER_URL, prepared_identity);
    TEST_ASSERT_EQUAL_MEMORY(&source.metadata, &prepared, sizeof(prepared));
    TEST_ASSERT_EQUAL_UINT(0, released);
    app_artwork_close();
    TEST_ASSERT_EQUAL_UINT(1, released);

    source.kind = AUDIO_SOURCE_NETWORK;
    accept      = 0;

    app_artwork_prepare(&source);
    TEST_ASSERT_EQUAL_UINT(1, network_takes);
    TEST_ASSERT_EQUAL_INT(ARTWORK_PER_TRACK, prepared_identity);
    TEST_ASSERT_EQUAL_UINT(2, released);
}

static void inactive_sources_clear_cover(void)
{
    const TrackMetadata empty      = { 0 };
    AudioSourceStatus   inactive[] = {
        { .kind = AUDIO_SOURCE_CHECKING },
        { .kind = AUDIO_SOURCE_CD },
        { .kind = AUDIO_SOURCE_NETWORK, .network_waiting = 1 },
        { .kind = AUDIO_SOURCE_NETWORK, .listening = 1 }
    };

    for (unsigned i = 0; i < sizeof(inactive) / sizeof(inactive[0]); ++i)
    {
        inactive[i].metadata = source.metadata;

        app_artwork_prepare(&inactive[i]);
        TEST_ASSERT_EQUAL_MEMORY(&empty, &prepared, sizeof(empty));
        TEST_ASSERT_FALSE(has_blob);
        TEST_ASSERT_EQUAL_INT(ARTWORK_PER_TRACK, prepared_identity);
    }

    TEST_ASSERT_EQUAL_UINT(0, network_takes + cd_takes);
}

static void decoder_failure_remains_optional(void)
{
    app_artwork_close();

    decoder_available = 0;

    TEST_ASSERT_TRUE(!(app_artwork_open() == 0));

    download = 1;

    app_artwork_prepare(&source);
    TEST_ASSERT_EQUAL_UINT(1, released);
}

#if STROOM_DIAGNOSTICS
static void diagnostic_wiring(void)
{
    TEST_ASSERT_TRUE(registered_observer == network_artwork_observe);
    registered_observer(&source.metadata, (ArtworkObservation){ .phase = DIAGNOSTIC_ARTWORK_UPLOAD_BEGIN });
    TEST_ASSERT_EQUAL_UINT(1, observations);
    TEST_ASSERT_EQUAL_INT(DIAGNOSTIC_ARTWORK_UPLOAD_BEGIN, observation.phase);
    app_artwork_close();
    TEST_ASSERT_NULL(registered_observer);
}
#endif

static void close_failure_preserves_observer_and_ownership(void)
{
    download = accept = 1;

    app_artwork_prepare(&source);

    close_ok = 0;

    TEST_ASSERT_TRUE(!(app_artwork_close() == 0));
    TEST_ASSERT_TRUE(!(app_artwork_open() == 0));
    TEST_ASSERT_NOT_NULL(accepted);
    TEST_ASSERT_EQUAL_UINT(0, released);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NOT_NULL(registered_observer);
#endif
    close_ok = 1;

    TEST_ASSERT_TRUE(app_artwork_close() == 0);
    TEST_ASSERT_NULL(accepted);
    TEST_ASSERT_EQUAL_UINT(1, released);
#if STROOM_DIAGNOSTICS
    TEST_ASSERT_NULL(registered_observer);
#endif
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(source_identity_and_ownership);
    RUN_TEST(close_failure_preserves_observer_and_ownership);
    RUN_TEST(inactive_sources_clear_cover);
    RUN_TEST(decoder_failure_remains_optional);
#if STROOM_DIAGNOSTICS
    RUN_TEST(diagnostic_wiring);
#endif

    return UNITY_END();
}
