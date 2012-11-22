<?php
/**
 * @file
 * Functions that are used for sending notification emails.
 */

/**
 * Sends by email a notification message.
 *
 * The subject and body of the message depend on the $params->type.
 * Other required attributes of $params are $params->recipient
 * and $params->uid. The other attributes are needed to build the
 * subject and body of the message. Some common attributes are:
 *   $params->username, $params->sguid, $params->string,
 *   $params->translation, etc.
 */
function l10n_feedback_send_notification_by_email($params) {
  // See: http://api.drupal.org/api/drupal/includes%21mail.inc/function/drupal_mail/7
  $account = user_load($params->uid);
  drupal_mail(
    $module   = 'l10n_feedback',
    $key      = 'notifications',
    $to       = $params->recipient,
    $langcode = $account->language,
    $params   = l10n_feedback_build_notification_email($params),
    $from     = l10n_feedback_get_email_sender(),
    $send     = TRUE
  );
}

/**
 * Returns the subject and body of a notification email:
 *    array('subject' => $subject, 'body' => $body)
 *
 * The subject and body of the message depend on the type
 * of the notification, defined by $params->type.
 *
 * The other attributes depend on the notification type.
 * Some common attributes are: $params->uid, $params->sguid,
 * $params->username, $params->string, $params->translation, etc.
 */
function l10n_feedback_build_notification_email($params) {

  $url = get_string_url($params->uid, $params->sguid);
  $subject_prefix = 'l10n-' . variable_get('l10n_feedback_translation_lng', 'fr');

  switch ($params->type) {

    // Send an email notification to a user, when a translation that he
    // has voted for is modified.
    case 'notify-voter-on-translation-change':
      $subject = t("[!prefix] Translation modified for: !string",
                 array(
                   '!prefix' => $subject_prefix,
                   '!string' => l10n_feedback_shorten($params->string, 30),
                 ));
      $body = array(
        t("Greetings !username,

           The following translation has been modified by its author.
           You are being notified because you have voted for the old
           translation and your vote now counts for the new version
           of the translation.

           If the new version of the translation is OK for you,
           then there is nothing you should do. Otherwise, you
           can change your vote at this link: !url

           Thanks

           ---------- Original (English) String:
           !string
           ---------- Old Translation:
           !old_translation
           ---------- New Translation:
           !new_translation
           ----------
           ",
          array(
            '!username' => $params->username,
            '!url' => $url,
            '!string' => $params->string,
            '!old_translation' => $params->old_translation,
            '!new_translation' => $params->new_translation,
          )),
      );
      break;

    // Notify the previous voters of a string that a new translation has been
    // submitted. Maybe they would like to review it and change their vote.
    case 'notify-voter-on-new-translation':
      $subject = t("[!prefix] New translation for: !string",
                 array(
                   '!prefix' => $subject_prefix,
                   '!string' => l10n_feedback_shorten($params->string, 30),
                 ));
      $body = array(
        t("Greetings !username,

           A new translation has been suggested for a string
           that you have voted for. If you think that the new
           translation is more suitable than the one that you
           have voted for, then you can change your vote
           at this link: !url

           Thanks

           ---------- Original (English) String:
           !string
           ---------- Voted Translation:
           !voted_translation
           ---------- New Translation:
           !new_translation
           ----------
          ",
          array(
            '!username' => $params->username,
            '!url' => $url,
            '!string' => $params->string,
            '!voted_translation' => $params->voted_translation,
            '!new_translation' => $params->new_translation,
          )),
      );
      break;

    // Notify the author of a translation, when it is deleted.
    case 'notify-author-on-translation-deletion':
      $subject = t("[!prefix] Translation deleted: !translation",
                 array(
                   '!prefix' => $subject_prefix,
                   '!translation' => l10n_feedback_shorten($params->translation, 30),
                 ));
      $body = array(
        t("Greetings !username,

           A translation that you have submitted, has been deleted by a moderator.
           You can see the string and the rest of translations at this link: !url

           Thanks

           ---------- Original (English) String:
           !string
           ---------- Deleted Translation:
           !translation
           ----------
           ",
          array(
            '!username' => $params->username,
            '!url' => $url,
            '!string' => $params->string,
            '!translation' => $params->translation,
          )),
      );
      break;

    // Notify the voters of a translation, when it is deleted.
    case 'notify-voter-on-translation-deletion':
      $subject = t("[!prefix] Translation deleted: !translation",
                 array(
                   '!prefix' => $subject_prefix,
                   '!translation' => l10n_feedback_shorten($params->translation, 30),
                 ));
      $body = array(
        t("Greetings !username,

           A translation that you have voted, has been deleted by a moderator.
           You can see the string and the rest of translations at this link: !url

           Thanks

           ---------- Original (English) String:
           !string
           ---------- Deleted Translation:
           !translation
           ----------
           ",
          array(
            '!username' => $params->username,
            '!url' => $url,
            '!string' => $params->string,
            '!translation' => $params->translation,
          )),
      );
      break;

    // Send by email a string to be reviewed.
    case 'string-to-be-reviewed':
      list($string, $translations) = l10n_feedback_get_a_string($params->uid);
      $subject = "[$subject_prefix] " . l10n_feedback_shorten($string, 60);
      $body = array(
        t("Greetings !username,

       If you have time, please help to review the translation
       of the string at this link: !url

       Thanks

       ---------- Original (English) String:
       !string
       ---------- Available Translations:
      ",
          array(
            '!username' => $params->username,
            '!url' => get_string_url($params->uid, $sguid),
            '!string' => $string,
          )),
      );
      foreach ($translations as $translation) {
        $body[] = ' * ' . $translation;
      }
      break;
  }

  return array(
    'subject' => $subject,
    'body' => $body,
  );
}


/**
 * Return a string (and its translations) for the given user:
 * return array($string, $translations)
 */
function l10n_feedback_get_a_string($uid) {
  // get a sguid according to the user preferencies
  require_once(dirname(__FILE__) . '/translations/vote.inc');
  $sguid = l10n_feedback_get_sguid($uid);

  // get the string and translations of the selected sguid
  $db = $GLOBALS['l10n_feedback_db'];
  $query = $db->select('l10n_feedback_strings', 's')
    ->fields('s', array('sguid'))
    ->where('s.sguid = :sguid', array(':sguid' => $sguid));
  $lng = variable_get('l10n_feedback_translation_lng', 'fr');
  $strings = l10n_feedback_get_strings($query, $lng);
  $string = $strings[$sguid]->string;
  $translations = array();
  foreach ($strings[$sguid]->translations as $tguid => $obj_translation) {
    $translations[] = $obj_translation->translation;
  }

  return array($string, $translations);
}


/**
 * Returns a one-time-login url for the given user,
 * which opens the string with the given id.
 * This url is used in mail notifications, so that
 * the user can quickly and easily login and edit
 * the given string.
 */
function get_string_url($uid, $sguid) {

  $account = user_load($uid);
  $lng = variable_get('l10n_feedback_translation_lng', 'fr');
  $timestamp = REQUEST_TIME;

  // get the url of the string
  $rehash = user_pass_rehash($account->pass, $timestamp, $account->login);
  $url = url("translations/$lng/$sguid/$timestamp.$uid.$rehash", array('absolute' => TRUE));

  // get a short url
  $short_url = get_short_url($url);
  if ($short_url == 'wait') {
    sleep(60);
    $short_url = get_short_url($url);
  }

  if ($short_url == NULL or $short_url == 'wait') {
    return $url;
  }
  else {
    return $short_url;
  }
}

/**
 * Get a short URL, using http://is.gd/ .
 */
function get_short_url($url) {

  $url = urlencode($url);
  $path = "http://v.gd/create.php?format=simple&url=$url";

  //We need to set a context with ignore_errors on otherwise PHP doesn't fetch
  //page content for failure HTTP status codes (v.gd needs this to return error
  //messages when using simple format)
  $opts = array("http" => array("ignore_errors" => TRUE));
  $context = stream_context_create($opts);

  $response = @file_get_contents($path, FALSE, $context);

  if (!isset($http_response_header)) {
    return NULL;
  }

  //Hacky way of getting the HTTP status code from the response headers
  if (!preg_match("{[0-9]{3}}", $http_response_header[0], $http_status)) {
    return NULL;
  }

  if ($http_status[0] == 200) {
    return $response;
  }
  elseif ($http_status[0] == 502) {
    return 'wait';
  }
  else {
    return NULL;
  }
}