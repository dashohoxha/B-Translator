<?php
/**
 * @file
 * Translation form submission.
 * The form may contain more than one translations.
 */

/**
 * Form submit callback for btranslator_data_translate_form().
 *
 * @see btranslator_data_translate_form().
 */
function btranslator_data_translate_form_submit($form, &$form_state) {

  $form_values = $form_state['values'];
  //dpm($form_values);  //debug

  // Check that the button  clicked was either Save, or Next, or Previous, etc.
  $op = $form_values['op'];
  if ( $op!=t('Login') && $op!=t('Previous') && $op!=t('Save') && $op!=t('Next')
       && $op!=t('Translated') && $op!=t('Untranslated') ) {
    return;
  }

  if ($op==t('Login')) {
    drupal_goto('user', array('query' => drupal_get_destination()));
  }

  // Save the values selected on the form (votes or new suggestions).
  if ( user_access('btranslator-vote') || user_access('btranslator-suggest') ) {
    _btranslator_data_translate_form_save($form_values);
  }
  elseif (!user_is_logged_in()) {
    drupal_set_message(t("You need to be logged in, in order to be able to submit votes and suggestions."));
  }

  $_SESSION['same_project'] = isset($form_state['input']['same_project']) ? TRUE : FALSE;
  if ($op == t('Previous')) {
    if (isset($_SESSION['previous_url'])) {
      $previous_url = $_SESSION['previous_url'];
      $_SESSION['previous_url'] = htmlentities(request_uri());
      drupal_goto($previous_url);
    }
  }
  elseif ($op == t('Save')) {
    // Keep existing filters and other query arguments on form submission.
    $redirect_args = $form_values['target'];
    unset($redirect_args['q']);
    $form_state['redirect'] = array($form_values['target']['q'], array('query' => $redirect_args));
  }
  else {
    $_SESSION['previous_url'] = htmlentities(request_uri());
    $lng = $form_values['langcode'];
    if ($op == t('Next')) {
      drupal_goto("translations/$lng/");
    }
    elseif ($op == t('Translated')) {
      drupal_goto("translations/$lng/translated");
    }
    elseif ($op == t('Untranslated')) {
      drupal_goto("translations/$lng/untranslated");
    }
  }
}

/**
 * Save the values selected on the form (votes or new suggestions).
 */
function _btranslator_data_translate_form_save($form_values) {

  $save_access = (user_access('btranslator-vote') || user_access('btranslator-suggest'));
  if (!$save_access) {
    drupal_set_message(t("You don't have access for giving feedback!"), 'error');
    return;
  }

  // get the langcode submitted with the form
  $lng = $form_values['langcode'];

  // check whether the submitted lng is valid for providing feedback on this site
  $translation_lng = variable_get('btr_translation_lng', 'fr');
  if ($lng != $translation_lng) {
    drupal_set_message(t("You cannot give feedback for the language '@lng' on this site!", array('@lng' => $lng)), 'error');
    return;
  }

  // Get the voting mode.
  $voting_mode = variable_get('btr_voting_mode', 'single');

  // keep track of the status, so that propper notification
  // can be displayed to the user
  global $_btranslator_data_save_status;
  $_btranslator_data_save_status = array(
    'cannot_vote' => 0,
    'cannot_suggest' => 0,
    'nr_votes' => 0,
    'nr_suggestions' => 0,
    'nr_deletions' => 0,
  );

  // Iterate outer structure built in btranslator_data_translate_form().
  foreach ($form_values['strings'] as $sguid => $string) {

    if ($voting_mode == 'single') {
      _btranslator_data_translate_form_submit_single($sguid, $lng, $string);
    }

    // Iterate inner structure built in _btranslator_data_translate_form_item().
    // Form items have numeric $tguid values and other keys here.
    foreach ($string as $tguid => $translation) {
      if ($voting_mode == 'multiple') {
        _btranslator_data_translate_form_submit_multiple($sguid, $tguid, $lng, $translation);
      }
      if ((strlen($tguid) == 40) && !empty($translation['declined'])) {
	// Delete translation.
	_btranslator_data_delete_translation($translation);
      }
    }
  }

  // Tell the user what happened.
  btranslator_data_display_notification_messages();
}

/**
 * Return true if a new translation has been submitted.
 */
function is_not_empty_translation($translation) {
  $translation = btranslator_data_pack_string($translation);
  $translation = str_replace(t('<New translation>'), '', $translation);
  $translation = trim($translation);
  return !empty($translation);
}

function _btranslator_data_translate_form_submit_single($sguid, $lng, $string) {

  if ( is_not_empty_translation($string['new']['value']) ) {
    // Add a new suggestion.
    btranslator_data_add_suggestion($sguid, $lng, $string['new']['value']);
    return;
  }

  $tguid = $string['approved'];
  if (strlen($tguid) == 40) {
    // If this is not an existing vote,
    // then add a new vote for this translation.
    $previous_votes = $string[$tguid]['original']->votes;
    global $user;
    if (!in_array($user->name, array_keys($previous_votes))) {
      $umail = $user->init;  // initial mail of the user
      btranslator_data_add_vote($tguid, $umail);
    }
  }
}

function _btranslator_data_translate_form_submit_multiple($sguid, $tguid, $lng, $translation) {

  global $user;

  $approved = $translation['approved'];
  if ($tguid == 'new' and is_not_empty_translation($translation['value'])) {
    btranslator_data_add_suggestion($sguid, $lng, $translation['value']);
  }
  elseif ($approved != '') {
    // Add a new vote for this translation
    // if such a vote does not exist.
    $previous_votes = $translation['original']->votes;
    if (!in_array($user->name, array_keys($previous_votes))) {
      $umail = $user->init;  // initial mail of the user
      btranslator_data_add_vote($tguid, $umail);
    }
  }
  elseif ($approved == '') {
    // Remove this vote, if it exists.
    $previous_votes = $translation['original']->votes;
    if (in_array($user->name, array_keys($previous_votes))) {
      $umail = $user->init;  // initial mail of the user
      _btranslator_data_clean_previous_votes($tguid, $umail, $sguid, $lng);
    }
  }
}

/**
 * Add a vote for the given translation from the given user.
 * Make sure that any previous vote is cleaned first
 * (don't allow multiple votes for the same translation).
 *
 * @param $tguid
 *   ID of the translation.
 * @param $umail
 *   The user email.
 * @return
 *   The ID of the new vote, or NULL.
 */
function btranslator_data_add_vote($tguid, $umail) {

  // Check access permissions.
  if (!user_access('btranslator-vote')) {
    global $_btranslator_data_save_status;
    $_btranslator_data_save_status['cannot_vote']++;
    return NULL;
  }

  // Fetch the translation details from the DB.
  $db = $GLOBALS['btr_db'];
  $sql = 'SELECT * FROM {btr_translations} WHERE tguid = :tguid';
  $args = array(':tguid' => $tguid);
  $trans = $db->query($sql, $args)->fetchObject();

  // If there is no such translation, return NULL.
  if (empty($trans))  return NULL;

  // Clean any previous vote.
  $nr = _btranslator_data_clean_previous_votes($tguid, $umail, $trans->sguid, $trans->lng);

  // Add the vote.
  $vid = $db->insert('btr_votes')
    ->fields(array(
        'tguid' => $tguid,
        'umail' => $umail,
        'ulng' => variable_get('btr_translation_lng', 'fr'),
        'time' => date('Y-m-d H:i:s', REQUEST_TIME),
      ))
    ->execute();

  // Update vote count of the translation.
  $sql = 'SELECT COUNT(*) FROM {btr_votes} WHERE tguid = :tguid';
  $count = $db->query($sql, $args)->fetchField();
  $db->update('btr_translations')
    ->fields(array('count' => $count))
    ->condition('tguid', $tguid)
    ->execute();

  // Increment the number of the saved votes.
  global $_btranslator_data_save_status;
  $_btranslator_data_save_status['nr_votes']++;

  return $vid;
}

/**
 * Clean any previous vote by this user for this translation.
 *
 * This depends on the voting mode option (set by the admin).
 * If the voting mode is 'single', then the user can select
 * only one translation for a given string (at most one vote
 * per string).
 * If the voting mode is 'multiple', then the user can approve
 * several translations for a string (at most one vote per
 * translation).
 *
 * @param $tguid
 *   ID of the translation.
 * @param $umail
 *   The mail of the user.
 * @param $sguid
 *   ID of the source string.
 * @param $lng
 *   Language code of the translation.
 * @return
 *   Number of previous votes that were deleted.
 *   (Normally should be 0, but can also be 1. If it is >1,
 *   something must be wrong.)
 */
function _btranslator_data_clean_previous_votes($tguid, $umail, $sguid, $lng) {

  $db = $GLOBALS['btr_db'];

  // Check access permissions.
  if (!user_access('btranslator-vote')) {
    global $_btranslator_data_save_status;
    $_btranslator_data_save_status['cannot_vote']++;
    return 0;
  }

  // Get the voting mode.
  $voting_mode = variable_get('btr_voting_mode', 'single');

  $arr_tguid = array();
  if ($voting_mode == 'multiple') {
    $arr_tguid = array($tguid);
  }
  else { // ($voting_mode == 'single')
    // Get the other sibling translations (translations of the same
    // string and the same language) which the user has voted.
    $sql = '
      SELECT DISTINCT t.tguid FROM {btr_translations} t
      LEFT JOIN {btr_votes} v ON (v.tguid = t.tguid)
      WHERE t.sguid = :sguid AND t.lng = :lng AND v.umail = :umail AND v.ulng = :ulng
    ';
    $params = array(':sguid' => $sguid, ':lng' => $lng, ':umail' => $umail, ':ulng' => $lng);
    $arr_tguid = $db->query($sql, $params)->fetchCol();
  }

  if (empty($arr_tguid))  return 0;

  // Insert to the trash table the votes that will be removed.
  $query = $db->select('btr_votes', 'v')
    ->fields('v', array('vid', 'tguid', 'umail', 'ulng', 'time', 'active'))
    ->condition('umail', $umail)
    ->condition('ulng', $lng)
    ->condition('tguid', $arr_tguid, 'IN');
  $query->addExpression('NOW()', 'd_time');
  $db->insert('btr_votes_trash')->from($query)->execute();

  // Remove any votes by the user for each translation in $arr_tguid.
  $num_deleted = $db->delete('btr_votes')
    ->condition('umail', $umail)
    ->condition('ulng', $lng)
    ->condition('tguid', $arr_tguid, 'IN')
    ->execute();

  // Decrement the vote count for each translation in $arr_tguid.
  $num_updated = $db->update('btr_translations')
    ->expression('count', 'count - 1')
    ->condition('tguid', $arr_tguid, 'IN')
    ->execute();

  return $num_deleted;
}

/**
 * Adds a new translation suggestion to a source string.
 *
 * @param $sguid
 *   The string ID for which a new suggestion should be added.
 * @param $lng
 *   The language code of the new suggestion.
 * @param $values
 *   Array of strings containing the new translation suggestion
 *   for the singular version and for plural version(s) (if any).
 * @param $umail
 *   The mail of the user that submitted the suggestion. If missing,
 *   the mail of the logged in user will be used.
 */
function btranslator_data_add_suggestion($sguid, $lng, $values, $umail =NULL) {

  if ($umail == NULL) {
    global $user;
    $umail = $user->init;
  }

  $translation = btranslator_data_pack_string($values);
  $translation = str_replace(t('<New translation>'), '', $translation);

  // Don't add empty translations.
  if (trim($translation) == '')  return FALSE;

  // Check access permissions.
  if (!user_access('btranslator-suggest')) {
    global $_btranslator_data_save_status;
    $_btranslator_data_save_status['cannot_suggest']++;
    return FALSE;
  }

  // Load the string and adjust translation whitespace based on source.
  $db = $GLOBALS['btr_db'];
  $query = 'SELECT string FROM {btr_strings} WHERE sguid = :sguid';
  $source_string = $db->query($query, array(':sguid' => $sguid))->fetchField();
  $translation = btranslator_data_trim($translation, $source_string);

  // Look for an existing translation, if any.
  $tguid = sha1($translation . $lng . $sguid);
  $query = 'SELECT translation FROM {btr_translations} WHERE tguid = :tguid';
  $existing = $db->query($query, array(':tguid' => $tguid))->fetchField();

  // If this translation already exists, there is nothing to be added.
  if (!empty($existing))  return FALSE;

  // Insert the new suggestion.
  $db->insert('btr_translations')
    ->fields(array(
        'sguid' => $sguid,
        'lng' => $lng,
        'translation' => $translation,
        'tguid' => $tguid,
        'count' => 1,
        'umail' => $umail,
        'ulng' => $lng,
        'time' => date('Y-m-d H:i:s', REQUEST_TIME),
      ))
    ->execute();

  // If there is another translation for the same string, by the same user,
  // the new translation should replace the old one. This is useful when
  // the user wants to correct the translation, but it limits the user to
  // only one suggested translation per string.
  btranslator_data_remove_old_suggestion($sguid, $lng, $umail, $tguid, $translation);

  // Add also a vote for the new suggestion.
  $vid = btranslator_data_add_vote($tguid, $umail);

  // Notify previous voters of this string that a new suggestion has been
  // submitted. Maybe they would like to review it and change their vote.
  btranslator_data_notify_previous_voters($sguid, $lng, $tguid, $translation);

  // Increment the number of the saved suggestions.
  global $_btranslator_data_save_status;
  $_btranslator_data_save_status['nr_suggestions']++;

  return $tguid;
}

/**
 * If there is another translation for the same string, by the same user,
 * the new translation should replace the old one. This is useful when
 * the user wants to correct the translation, but it limits the user to
 * only one suggested translation per string.
 */
function btranslator_data_remove_old_suggestion($sguid, $lng, $umail, $tguid, $translation) {

  // Allow translators to add more than one suggestions for the same string.
  if (user_access('btranslator-import'))  return;

  $db = $GLOBALS['btr_db'];

  // Get the old translation (if any).
  $query = 'SELECT tguid, translation
            FROM {btr_translations}
            WHERE sguid = :sguid AND lng = :lng
              AND umail = :umail AND ulng = :ulng
              AND tguid != :tguid';
  $args = array(
    ':sguid' => $sguid,
    ':lng' => $lng,
    ':umail' => $umail,
    ':ulng' => $lng,
    ':tguid' => $tguid);
  $old_trans = $db->query($query, $args)->fetchObject();
  if (!$old_trans)  return;  // if there is no old translation, we are done

  // Copy to the trash table the old translation.
  $query = $db->select('btr_translations', 't')
    ->fields('t', array('sguid', 'lng', 'translation', 'tguid', 'count', 'umail', 'ulng', 'time', 'active'))
    ->condition('tguid', $old_trans->tguid);
  $query->addExpression(':d_umail', 'd_umail', array(':d_umail' => $umail));
  $query->addExpression(':d_ulng', 'd_ulng', array(':d_ulng' => $lng));
  $query->addExpression('NOW()', 'd_time');
  $db->insert('btr_translations_trash')->from($query)->execute();

  // Remove the old translation.
  $db->delete('btr_translations')
    ->condition('tguid', $old_trans->tguid)
    ->execute();

  // Get the votes of the old translation.
  $query = "SELECT v.tguid, v.time, u.umail, u.ulng, u.uid,
                   u.name AS user_name, u.status AS user_status
            FROM {btr_votes} v
            LEFT JOIN {btr_users} u ON (u.umail = v.umail AND u.ulng = v.ulng)
            WHERE v.tguid = :tguid AND v.umail != :umail";
  $args = array(':tguid' => $old_trans->tguid, ':umail' => $umail);
  $votes = $db->query($query, $args)->fetchAll();

  // Insert to the trash table the votes that will be deleted.
  $query = $db->select('btr_votes', 'v')
    ->fields('v', array('vid', 'tguid', 'umail', 'ulng', 'time', 'active'))
    ->condition('tguid', $old_trans->tguid);
  $query->addExpression('NOW()', 'd_time');
  $db->insert('btr_votes_trash')->from($query)->execute();

  // Delete the votes belonging to the old translation.
  $db->delete('btr_votes')->condition('tguid', $old_trans->tguid)->execute();

  // Associate these votes to the new translation.
  $notifications = array();
  foreach ($votes as $vote) {
    // Associate the vote to the new translation.
    $db->insert('btr_votes')
      ->fields(array(
          'tguid' => $tguid,
          'umail' => $vote->umail,
          'ulng' => $vote->ulng,
          'time' => $vote->time,
        ))
      ->execute();

    if ($vote->user_status != 1)  continue;   // skip non-active voters
    // Create a notification.
    $notifications[] = (object) array(
      'uid' => $vote->uid,
      'username' => $vote->user_name,
      'recipient' => $vote->user_name . ' <' . $vote->umail . '>',
    );
  }

  if (empty($notifications))  return;

  // get the original string
  $query = "SELECT string FROM {btr_strings} WHERE sguid = :sguid";
  $string = $db->query($query, array(':sguid' => $sguid))->fetchField();

  // Notify the corresponding voters about the change
  $queue = DrupalQueue::get('notifications');
  $queue->createQueue();  // There is no harm in trying to recreate existing.
  foreach ($notifications as $notify) {
    $notification_params = array(
      'type' => 'notify-voter-on-translation-change',
      'uid' => $notify->uid,
      'username' => $notify->username,
      'recipient' => $notify->recipient,
      'sguid' => $sguid,
      'string' => $string,
      'old_translation' => $old_trans->translation,
      'new_translation' => $translation,
    );
    $queue->createItem((object)$notification_params);
  }
}

/**
 * Notify the previous voters of a string that a new translation has been
 * submitted. Maybe they would like to review it and change their vote.
 */
function btranslator_data_notify_previous_voters($sguid, $lng, $tguid, $translation) {

  $db = $GLOBALS['btr_db'];

  $query = "SELECT u.umail, u.ulng, u.uid, u.name, u.status, t.translation
            FROM {btr_translations} t
            LEFT JOIN {btr_votes} v ON (v.tguid = t.tguid)
            LEFT JOIN {btr_users} u ON (u.umail = v.umail AND u.ulng = v.ulng)
            WHERE t.sguid = :sguid AND t.lng = :lng AND t.tguid != :tguid";
  $args = array(':sguid' => $sguid, ':lng' => $lng, ':tguid' => $tguid);
  $voters = $db->query($query, $args)->fetchAll();

  if (empty($voters))  return;

  // get the original string
  $query = "SELECT string FROM {btr_strings} WHERE sguid = :sguid";
  $string = $db->query($query, array(':sguid' => $sguid))->fetchField();

  // Notify the previous voters about the new translation
  $queue = DrupalQueue::get('notifications');
  $queue->createQueue();  // There is no harm in trying to recreate existing.
  foreach ($voters as $voter) {
    $notification_params = array(
      'type' => 'notify-voter-on-new-translation',
      'uid' => $voter->uid,
      'username' => $voter->name,
      'recipient' => $voter->name . ' <' . $voter->umail . '>',
      'sguid' => $sguid,
      'string' => $string,
      'voted_translation' => $voter->translation,
      'new_translation' => $translation,
    );
    $queue->createItem((object)$notification_params);
  }
}

/**
 * Delete the translation and any related votes.
 * Notify the author and the voters about the deletion.
 *
 * $translation is an array/object structure with all the
 * information about the translation, votes, etc.
 */
function _btranslator_data_delete_translation($translation) {

  //dpm($translation);  //debug
  $trans = $translation['original'];

  // Check that the current user has the right to delete translations.
  $is_own = ($GLOBALS['user']->name == $trans->author);
  if (!$is_own and !user_access('btranslator-resolve')) {
    drupal_set_message(t('You are not allowed to delete this translation!'), 'error');
    return;
  }

  $db = $GLOBALS['btr_db'];

  // Copy to the trash table the translations that will be deleted.
  $query = $db->select('btr_translations', 't')
    ->fields('t', array('sguid', 'lng', 'translation', 'tguid', 'count', 'umail', 'ulng', 'time', 'active'))
    ->condition('tguid', $trans->tguid);
  $account = user_load($GLOBALS['user']->uid);
  $query->addExpression(':d_umail', 'd_umail', array(':d_umail' => $account->init));
  $query->addExpression(':d_ulng', 'd_ulng', array(':d_ulng' => variable_get('btr_translation_lng')));
  $query->addExpression('NOW()', 'd_time');
  $db->insert('btr_translations_trash')->from($query)->execute();

  // Copy to the trash table the votes that will be deleted.
  $query = $db->select('btr_votes', 'v')
    ->fields('v', array('vid', 'tguid', 'umail', 'ulng', 'time', 'active'))
    ->condition('tguid', $trans->tguid);
  $query->addExpression('NOW()', 'd_time');
  $db->insert('btr_votes_trash')->from($query)->execute();

  // Delete the translation and any votes related to it.
  $db->delete('btr_translations')->condition('tguid', $trans->tguid)->execute();
  $db->delete('btr_votes')->condition('tguid', $trans->tguid)->execute();

  // get the original string
  $query = "SELECT string FROM {btr_strings} WHERE sguid = :sguid";
  $string = $db->query($query, array(':sguid' => $trans->sguid))->fetchField();

  // Notify the author of the translation about the deletion.
  $queue = DrupalQueue::get('notifications');
  $queue->createQueue();  // There is no harm in trying to recreate existing.
  $notification_params = array(
    'type' => 'notify-author-on-translation-deletion',
    'uid' => $trans->uid,
    'username' => $trans->author,
    'recipient' => $trans->author . ' <' . $trans->umail . '>',
    'sguid' => $trans->sguid,
    'string' => $string,
    'translation' => $trans->translation[0],
  );
  $queue->createItem((object)$notification_params);

  // Notify any voters of the translation as well.
  foreach ($trans->votes as $username => $vote) {
    if ($username == $trans->author)  continue;  // don't send a second message to the author
    $notification_params = array(
      'type' => 'notify-voter-on-translation-deletion',
      'uid' => $vote->uid,
      'username' => $vote->name,
      'recipient' => $vote->name . ' <' . $vote->umail . '>',
      'sguid' => $trans->sguid,
      'string' => $string,
      'translation' => $trans->translation[0],
    );
    $queue->createItem((object)$notification_params);
  }

  global $_btranslator_data_save_status;
  $_btranslator_data_save_status['nr_deletions']++;
}


/**
 * Make spacing and newlines the same in translation as in the source.
 *
 * @param $translation
 *   Translation string.
 * @param $source
 *   Source string.
 * @return
 *   Translation string with the right beginning and ending chars.
 */
function btranslator_data_trim($translation, $source) {
  $matches = array();
  preg_match("/^(\s*).*\S(\s*)\$/s", $source, $matches);
  return $matches[1] . trim($translation) . $matches[2];
}

function btranslator_data_display_notification_messages() {

  global $_btranslator_data_save_status;
  extract($_btranslator_data_save_status);

  if ($cannot_vote > 0) {
    drupal_set_message(t('You do not have enough rights for submitting votes!'), 'error');
  }

  if ($cannot_suggest > 0) {
    drupal_set_message(t('You do not have enough rights for making suggestions!'), 'error');
  }

  if ($nr_suggestions > 0) {
    drupal_set_message(format_plural($nr_suggestions, '1 suggestion saved', '@count suggestions saved'));
  }

  if ($nr_votes > 0) {
    drupal_set_message(format_plural($nr_votes, '1 vote saved', '@count votes saved'));
  }

  if ($nr_deletions > 0) {
    drupal_set_message(format_plural($nr_deletions, '1 translation deleted', '@count translations deleted'));
  }
}